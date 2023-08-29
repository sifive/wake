use argmin::core::CostFunction;
use argmin::core::Error;
use argmin::core::Gradient;
use argmin::core::Hessian;
use argmin::core::Jacobian;
use entity::job;
use entity::job_uses;
use nalgebra::Vector3;
use sea_orm::EntityTrait;
use sea_orm::{DatabaseConnection, DbErr};
use std::collections::HashMap;
use std::sync::Arc;

struct UseTimes {
    start_time: f64,
    end_time: f64,
    use_times: Vec<f64>,
}

// TODO: We should use lambda(t) = abs(a)*exp(-((t - b)/c)^2) instead
//
// First we define our function that we wish to fit our rate of use to:
//
//       lambda(t) = abs(a)*exp(-((t - b)/c)^2)
//
//
// We won't use these but for completeness these are the derivatives. In classic exponential
// fashion they're simple to state in terms of the origional function:
//
//       D(lambda; a) =           lambda(t) / a
//       D(lambda; b) = -2      * lambda(t) / c
//       D(lambda; c) = 2*(t-b) * lambda(t) / c^2
//
// The reason we choose this function is because it looks like a bell curve. In a job cache
// we see uses slowly ramp up, plateu for a while, and then slowly descend in usage at
// about the same rate that it ramped up at. A bell curve captures this well and is always
// non-zero. Granted, it has no plateu so it will sometimes over estimate and sometimes under
// estimate future usage relative to the more expected model which is peicewise linear. Using
// a piecewise linear function with 4 parameters to define a symetric piece wise linear
// function makes the math somewhat tricky but such a thing should be possible in the future.
// You also need to account for the fact that it may not produce a good optimization surface
// so you may need to smooth that piecewise linear function out some. To smooth it out you'd
// need to give a slight curve to the plataeu, and you'd need to give a slight curve from
// -inf, to the start of the ramp, etc... after you consider these two things the bell
// curve starts to look extremly close to the "ideal" function and it should produce
// an excelent optimization surface. Even with a bell curve however, if our curve
// is too far away from the sample points we've observed, our optimization surface will
// be nearly flat and will take a very long time to converge. We will cover initital estimates
// later in this comment.
//
// In order to turn this usage rate into a probability distribution that we can compute expected
// values from we need to define a probability distribution from this rate of use. If samples
// are independent (they're not really but should be close) then this is known as an
// 1D inhomogeneous Poisson process: https://en.wikipedia.org/wiki/Poisson_point_process
//
// The probability density (likelihood) of sampling a given set of points N between times
// O and T is given by:
//
// L(N) = exp(-int(0, T, lambda(t))) * prod(i=1, |N|, lambda(N_i))
//
// Sources:
// https://www1.maths.leeds.ac.uk/~voss/projects/2012-Poisson/Drazek.pdf
// https://en.wikipedia.org/wiki/Poisson_point_process#Inhomogeneous_Poisson_point_process
//
// NOTE: I belive the leeds source has a small error. It mixes up
//       the ordered and unordered case. To go from ordered to unordered you multiply by
//       n!, not the other way around. Proof: Take L_o(N) to be the *ordered* likelihood.
//       then we can take the product over all permutations of `N` (each of which has an
//       equal likelihood) to get prod(perms P of 1 to |N|, L_o(P(N))) = L_o(N)*|N|!
//
// NOTE: At time of writing, the wikipedia source is somewhat confusingly stated. It defines
//       the probability function over a sequence of intervals but this is quite a confusing
//       presentation for our purposes. We'd like the probability density over a single
//       interval instead.
//
// The integral in the exponent is often called "intensity" in litature around Poission proccses.
// You can think of it as the normalization constant for our lambda. In our case it can be computed
// using the error function:
//
//       I = int(0, T, lambda(t))
//         = abs(a)*sqrt(pi)/2*b*(erf(b/c) - erf((b - T)/c):)
//
// You can then compute the likelihood of any specific point t as being lambda(t)/I. If
// you then compute the probability of observing any K points as exp(-I)*I^K/n! (using the wikipedia
// formula with a single interval) you can multiply the likelihood of each point to get
// exp(-I)*I^K/n!*prod(i=1, |N|, lambda(N_i)/I) = L(N)
//
// NOTE: This derivation is pulled from the leeds source but I connect it back to the wikipedia
// source as well
//
// You can then take the log of this to get log-likelihood which removes an exponential, and a
// large product which should make our calculations much more numerically stable and sound. It
// has the added bonus of making it quite simple to take dirivitives.
//
// l(N) = log(L(n)) = -I + sum(1, |N| log(lambda(N_i)))
//
// It turns out we can drastically speed up repeated computations of l(N) as changes occur to
// a, b, and c but not N if we store the two following sums for a given N (these will also be
// useful later in computing our initital values for a, b, and c):
//
//       s1 = sum(1, |N|, N_i)
//       s2 = sum(1, |N|, N_i^2)
//
// We can then expand l(N) out in terms of s1, s2, I, a, b, and c...
//
//       l(N) = log(L(N))
//            = -int(0, T, lambda(t)) + sum(i=1, |N|, log(lambda(N_i))
//            = -I + sum(i=1, |N|, log(lambda(N_i)))
//            = -I + |N|*abs(a) - sum(i=1, |N|, (N_i - b)^2/c^2)
//            = ...            - 1/c^2*sum(i=1, |N|, (N_i - b)^2)
//            = ...            - 1/c^2*sum(i=1, |N|, N_i^2 - 2*b*N_i + b^2)
//            = ...            - 1/c^2*sum(i=1, |N|, N_i^2) + 2*b/c^2*sum(i=1, |N|, N_i) - |N|*b^2/c^2
//            = -I + |N|*abs(a) - s2/c^2 + 2*b*s1/c^2 - |N|*b^2/c^2
//
// ...which can be computed in O(1) time if s1 and s2 are pre-computed.
//
//
// It's handy to have some helpers as we go into the next part:
//
//       bell(x) = 2/sqrt(pi)*exp(-x^2)
//       coeff = -abs(a)*sqrt(pi)/2
//       r1 = b/c
//       r2 = (b-T)/c
//       NOTE: I = coeff*b*(erf(r1) - erf(r2)/c)
//
// W thene define the dirivitive of I with respect to b and c (not A because we don't need it)
//       D(I; b) = coeff * (bell(r1)/c - bell(r2)/c) + I/b
//       D(I; c) = coeff * (r2*bell(r2)/c - r1*bell(r1)/c)
//
// And in terms of that we can define the gradiant of l(N) with respect to [a, b, c]
//
//       D(l; a) = I/a + |N|
//       D(l; b) = D(I; b) + 2*s1/c^2 - 2|N|*b/c^2
//       D(l; c) = D(I; c) + 2*s2/c^3 - 4*b*s1/c^3 + 2*|N|*b^2/c^3
//
// Now that we have all those definitions out of the way the rest is fairly simple.
// We will use the argmin library to find a good maximum likelihood estimator for l(N).
// We'll take the observed set of use times, then repeatedly compute l(N) and grad(l; [a, b, c])
// to converge quickly towards a near optimal estimator which should give us a good estimate
// of future uses for any time range from T to T+epsilon.
impl CostFunction for UseTimes {
    type Param = Vector3<f64>;
    type Output = f64;

    // This function calculates the negative log likelihood of a given set
    // of use times assuming use times are distributed according to a 1D
    // inhomogeneous Poisson process with lambda(t) = A*t^2 + B*t + C
    // where A, B, and C are given by the respective values in the
    // UseTimeParams function.
    fn cost(&self, p: &Self::Param) -> Result<Self::Output, Error> {
        // The log likelihood of a set of points N over the time interval 0 to T
        // for a 1D inhomogeneous poisson process is:
        // l(lambda; N) = - int(0, T, lambda(t) * dt) + sum(i=1, |N|, log(lambda(N_i))
        //
        // with lambda(t) = A*t^2 + B*t + C
        // we have int(0, T, lambda(t) * dt) = A*T^3/3 + B*T^2/2 + C*T
        //
        // what follows is the computation of sum(i=1, |N|, log(lambda(N_i))
        // followed by the computation of the solved intergal
        //
        // NOTE: We use the log liklihood because its more numericaly stable
        //       and overall a bit easier to work with mathmatically.
        // NOTE: For details on this see the following links:
        // * https://www1.maths.leeds.ac.uk/~voss/projects/2012-Poisson/Drazek.pdf
        // * https://en.wikipedia.org/wiki/Poisson_point_process#Defined_on_the_real_line
        // NOTE: There is an ordered and unordered case but they only differ by a
        //       factor which is constant with respect to the parameters. It turns out
        //       that some factorials we'd rather not deal with cancel out in the ordered
        //       case so we use those.

        // Compute: sum(i=1, |N|, log(lambda(N_i))
        // TODO: Is there a faster way to do this in constant time if we pre-compute
        //       something?
        let mut sum = 0.0;
        for use_time in &self.use_times {
            let t = self.end_time - use_time;
            let t2 = t * t;
            sum += (p[0] * t2 + p[1] * t + p[2]).ln();
        }

        // Compute: int(0, T, lambda(t) * dt)
        let time = self.end_time - self.start_time;
        let time2 = time * time;
        let time3 = time2 * time;
        let intensity = p[0] * time3 / 3.0 + p[1] * time2 / 2.0 + p[2] * time;

        // Return negative the log-likelihood for the ordered case
        Ok(intensity - sum)
    }
}

impl Gradient for UseTimes {
    type Param = Vector3<f64>;
    /// The UseTimes cost function is in R^3 -> R
    // so the gradient is the same type
    type Gradient = Vector3<f64>;

    /// Compute the gradient at parameter `p`.
    fn gradient(&self, p: &Self::Param) -> Result<Self::Gradient, Error> {
        // The dirivitive of the log likelihood function with respect to each
        // param is fairly simple to compute (here df/dx means the *partial* dirivitive with respect to x)
        //
        // dl(lambda; N)/dA = -T^3/3 + sum(1, |N|, N_i^2 / lambda(N_i))
        //
        // dl(lambda; N)/dB = -T^2/2 + sum(1, |N|, N_i / lambda(N_i))
        //
        // dl(lambda; N)/dC = -T

        // So first we add up the two sums since they use essentially the same computation
        let mut a_sum = 0.0;
        let mut b_sum = 0.0;

        for use_time in &self.use_times {
            let t = self.end_time - use_time;
            let t2 = t * t;
            let denom = p[0] * t2 + p[1] * t + p[2];
            a_sum += t2 / denom;
            b_sum += t / denom;
        }

        // Then we compute the other parts
        let time = self.end_time - self.start_time;
        let time2 = time * time;
        let time3 = time2 * time;

        // NOTE: We're returning the gradiant of the negative of the log likelihood
        //       so the signs are flipped from the gradiant given above
        Ok(Vector3::new(time3 / 3.0 - a_sum, time2 / 2.0 - b_sum, time))
    }
}

// UseTime's cost function is R^3 -> R
// so the Jacobian and the Gradiant are the same.
impl Jacobian for UseTimes {
    type Param = Vector3<f64>;
    type Jacobian = Vector3<f64>;

    fn jacobian(&self, param: &Self::Param) -> Result<Self::Jacobian, Error> {
        self.gradient(param)
    }
}

// This function winds up being very expensive to compute, we should not run it regularlly
pub async fn rank_jobs(conn: Arc<DatabaseConnection>) -> Result<Vec<job::Entity>, DbErr> {
    let jobs_by_id: HashMap<i32, job::Model> = job::Entity::find()
        .all(&*conn)
        .await?
        .into_iter()
        .map(|j| (j.id, j))
        .collect();
    // TODO: Limit this to some time frame
    // NOTE: This list could contain a job that is not in the above hash map because we did not use a transaction.
    let all_uses = job_uses::Entity::find().all(&*conn).await?;
    let mut job_total_value = HashMap::new();
    let mut job_use_times: HashMap<i32, Vec<_>> = HashMap::new();
    for job_use in &all_uses {
        // Not only do we need to handle the Option here, but we might legitmately encounter
        // it because we didn't use a transaction above.
        let Some(job) = jobs_by_id.get(&job_use.job_id)
        else {
            continue;
        };

        // Update the compute saved per byte
        let cpu_hours_saved_per_byte = job.cputime / (job.o_bytes as f64);
        job_total_value
            .entry(job_use.job_id)
            .and_modify(|e| *e += cpu_hours_saved_per_byte)
            .or_insert(cpu_hours_saved_per_byte);

        // Update uses
        job_use_times
            .entry(job_use.job_id)
            .and_modify(|e| e.push(job_use.time))
            .or_insert(Vec::new());
    }

    // Now for each job we need to estimate its use pattern by fitting
    // it to a 1D inhomogeneous Poisson process with a second degree
    // polynomial as its lambda parameter. We do this via gradiant
    // descent for now. We expect the optimization surface to be
    // quite smooth so we expect gradiant descent to work well.
    for (job_id, usage_list) in  
    
    todo!()
}

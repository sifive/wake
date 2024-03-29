use chrono::NaiveDateTime;
use clap::Parser;
use crossterm::style::{Color, StyledContent, Stylize};
use miette::Error;
use miette::IntoDiagnostic;
use palette::{convert::FromColorUnclamped, encoding::Srgb, rgb::Rgb, Lab};
use serde::{Deserialize, Serialize};
use serde_aux::prelude::*;
use serde_json::Value;
use std::fs::File;
use std::io::BufRead;
use std::{collections::HashMap, io::BufReader};

// TODO: elide repeated messages (but say how many are skipped)

// Here we implement an additive recurrence sequence
// which turns out to be a good way to select colors.
struct AdditiveRec {
    x: f32,
    y: f32,
    z: f32,
}

impl Iterator for AdditiveRec {
    type Item = (f32, f32, f32);

    fn next(&mut self) -> Option<Self::Item> {
        let out = (self.x, self.y, self.z);
        // Advance x by the fractional part of the golden ratio
        self.x = (self.x + 0.618304f32).rem_euclid(1.0f32);
        // Advance y by the fractional part of sqrt(2)
        self.y = (self.y + 0.5545497f32).rem_euclid(1.0f32);
        // Advance z by the fractional part of sqrt(3)
        self.z = (self.z + 0.308517f32).rem_euclid(1.0f32);
        Some(out)
    }
}

// While on one hand this is extremly simple, on another hand it uses
// some obtuse math concepts. There's a color space called CIELAB
// that behaves a bit better when sampling from it. If you sample two points
// a certain distance apart in CIELAB, the perceived distance is roughly proportional
// to the distance between those points. So if we can sample a lot of points that stay
// relatively distinct from each other we can consequently get nice distinct colors.
// Additionally CIELAB has a sort of "brightness" field so that we can keep a relatively
// consistent brightness.
//
// In order to sample distinct points we use quasi-monte color methods, specifically
// low-discrepancy sequences. These give us consistently spaced out values in any dimensions
// and the minimum area/volume between anytime sampled points is inversely related to the
// number of points sampled. So if we only sample a few points, our colors will be very
// distinct, as we sample more and more points they will get closer and closer together but
// they should do a quite good job of staying as far apart as possible. We're using an additive
// recurence which does quite a good job for this use case and is very simple.
fn generate_distinct_colors() -> impl Iterator<Item = Color> {
    AdditiveRec {
        x: 0.0f32,
        y: 0.0f32,
        z: 0.0f32,
    }
    .map(|point| {
        // Vary luminosity from 60 to 90. I just played around with it
        // and found this to be a good range
        let l = point.0 * 30.0f32 + 60.0f32;
        // TODO: Consider setting Chroma at max, and just adjusting hue
        //       or varying chroma just a little bit like we do luminosity.
        // A and B both range from -128 to 127
        let a = 255.0f32 * point.1 - 128.0f32;
        let b = 255.0f32 * point.2 - 128.0f32;
        // Add a new color that should be highly distinct from
        // all the other colors and still have decent brightness
        let cielab = Lab::new(l, a, b);

        // Going from CIELAB to rgb is complicated but luckily pallate does
        // that for us.
        let rgb: Rgb<Srgb, f32> = Rgb::from_color_unclamped(cielab);
        let rgb: Rgb<Srgb, u8> = rgb.into_linear().into_encoding();

        // Lastly since we're outputting to a terminal we can do a simple conversion
        // to crossterm's color type
        Color::Rgb {
            r: rgb.red,
            g: rgb.green,
            b: rgb.blue,
        }
    })
}

// Defaults in serde require a function name. This provides
// -1 as a default
fn neg_one() -> i32 {
    -1
}

#[derive(Debug, Serialize, Deserialize)]
struct Event {
    time: Option<NaiveDateTime>,
    #[serde(
        default = "neg_one",
        deserialize_with = "deserialize_number_from_string"
    )]
    pid: i32,
    level: Option<String>,
    message: Option<String>,
    #[serde(flatten)]
    extra: HashMap<String, Value>,
}

fn get_log_level_color(level: &Option<String>) -> Color {
    let Some(level) = level
    else {
      return Color::White;
    };

    match level.as_str() {
        "info" => Color::Grey,
        "warning" => Color::Yellow,
        "error" => Color::Red,
        _ => Color::White,
    }
}

fn read_lines_from(file: &str) -> Result<Vec<Event>, Error> {
    let file = File::open(file).into_diagnostic()?;
    let file = BufReader::new(file);
    let mut out = vec![];
    for line in file.lines() {
        let line = line.into_diagnostic()?;
        let event: Event = serde_json::from_str(&line).into_diagnostic()?;
        out.push(event);
    }
    Ok(out)
}

#[derive(Debug, Parser)]
#[command(author, version, about, long_about = None)]
struct LogViewOptions {
    files: Vec<String>,
}

// TODO: Add a no-color option
struct RenderConfig {
    max_width: u16,
    color_map: HashMap<i32, Color>,
}

fn render_line(config: &RenderConfig, event: &Event) -> Result<(), Error> {
    let mut to_render: Vec<StyledContent<String>> = vec![];
    let color = *config.color_map.get(&event.pid).unwrap_or(&Color::White);

    to_render.push(String::from("[").with(Color::White));

    to_render.push(
        event
            .time
            .map(|x| x.to_string())
            .unwrap_or("".into())
            .dark_magenta(),
    );

    to_render.push(String::from(", ").with(Color::White));

    to_render.push(
        event
            .level
            .clone()
            .unwrap_or("".into())
            .with(get_log_level_color(&event.level)),
    );

    to_render.push(String::from(", ").with(Color::White));
    to_render.push(event.pid.to_string().dark_green());
    to_render.push(String::from("] ").with(Color::White));

    let cur_width: usize = to_render.iter().map(|x| x.content().len()).sum();
    let max_width = config.max_width as usize - cur_width;

    let mut message = event
        .message
        .clone()
        .unwrap_or_else(|| "<no message>".into());

    if message.len() > max_width {
        message.truncate(max_width - 3);
        message += "...";
    }

    to_render.push(message.with(color));

    for renderable in to_render {
        print!("{}", renderable);
    }
    println!();
    Ok(())
}

fn render(color_map: HashMap<i32, Color>, events: Vec<Event>) -> Result<(), Error> {
    let (max_width, _height) = crossterm::terminal::size().into_diagnostic()?;
    let config = RenderConfig {
        max_width,
        color_map,
    };
    for event in &events {
        render_line(&config, event)?;
    }
    Ok(())
}

fn main() -> Result<(), Error> {
    //color_eyre::install()?;
    let mut colors = generate_distinct_colors();
    let opts = LogViewOptions::parse();
    let mut events: Vec<Event> = vec![];

    // Parse all the files
    for file in opts.files {
        events.extend(read_lines_from(file.as_str())?);
    }

    // TODO: We should polyfill the time of a log event as the average of its neighbors
    //       but highlight it so that the user knows its polyfilled
    // Sort the vector by time
    events.sort_by(|a, b| a.time.cmp(&b.time));

    // Assign colors to each pid, making sure to assign pid-less things white
    let mut color_map: HashMap<i32, Color> = HashMap::new();
    color_map.insert(-1, Color::White);
    for event in &events {
        color_map
            .entry(event.pid)
            .or_insert(colors.next().expect("infinite stream ran out?!"));
    }

    render(color_map, events)?;

    Ok(())
}

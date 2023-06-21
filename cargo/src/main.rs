
fn hello_world() {
    println!("Hello, world!");
}

enum MyType {
  A, B  
};

impl MyType {
  fn flip(&self) -> MyType {
    match self {
        MyType::A => todo!(),
        MyType::B => todo!(),
    }
  }
  fn new() -> MyType {
    MyType::A; 
  }
}

impl Default for MyType {
    fn default() -> Self {
        Self::new()
    }
}

fn main() {
  fun_name();
  let x: File = File::from_raw_fd(0);
  hello_world();
}

fn fun_name(x: MyType) {
    match x.flip() {
        MyType::A => todo!(),
        MyType::B => todo!(),
    };
}

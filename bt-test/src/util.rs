use std::time::Duration;

use bt_hci::param::BdAddr;

pub fn default<T: Default>() -> T {
    Default::default()
}

pub fn debug_addr(addr: BdAddr) -> String {
    addr.0
        .iter()
        .map(|f| format!("{:02x}", f))
        .collect::<Vec<_>>()
        .join(":")
}

pub trait Mean {
    type Output;

    fn mean(self) -> Self::Output;
}

impl<'a, T, I> Mean for T
where
    T: ExactSizeIterator<Item = I>,
    f32: std::iter::Sum<I>,
{
    type Output = f32;

    fn mean(self) -> Self::Output {
        let len = self.len();
        self.sum::<f32>() / len as f32
    }
}

pub struct Benchmark {
    name: String,
    total: Duration,
    sample_count: usize,
}

impl Drop for Benchmark {
    fn drop(&mut self) {
        println!(
            "benchmark {} {:?} {:?} {:?}",
            self.name,
            self.total.as_secs_f64(),
            self.sample_count,
            (self.total.as_secs_f64() / self.sample_count as f64),
        );
    }
}

impl Benchmark {
    pub fn new(name: impl ToString) -> Self {
        let name = name.to_string();
        let total = default();
        let sample_count = default();

        Self {
            name,
            total,
            sample_count,
        }
    }

    pub fn add(&mut self, duration: Duration) {
        self.total += duration;
        self.sample_count += 1;
    }
}

macro_rules! benchmark {
    ($ident:ident, $( $body:tt )*) => {
        thread_local! {
            pub static $ident: RefCell<Benchmark> = RefCell::new(Benchmark::new(format!("{}:{}", file!(), line!())));
        }

        let _benchmark_t0 = Instant::now();

        $( $body )*

        let _benchmark_t1 = Instant::now();

        $ident.with(|b| b.borrow_mut().add(_benchmark_t1 - _benchmark_t0));
    };

    ($( $body:tt )*) => {
        gensym::gensym! {
            benchmark! { $( $body )* }
        }
    };
}
pub(crate) use benchmark;

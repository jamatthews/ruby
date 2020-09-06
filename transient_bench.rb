require 'benchmark'
N = 10_000_000

def n_times str, args = ''
  eval <<-EOS
    proc{|max, #{args}|
      i = 0
      while i < max
        #{str}
        i+=1
      end
    }
  EOS
end

m = n_times 'ary = Array.new(size)', 'size'

Benchmark.bm(10){|x|
  0.step(to: 16){|i|
    size = i
    x.report(size){
      m.call(N, size)
    }
  }
}

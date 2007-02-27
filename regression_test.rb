#!/bin/env ruby
require 'benchmark'
require 'optparse'

module RegressionTest
  EXIT_SUCCESS=0
  EXIT_FAILURE=99
  PASS=1; FAIL=2; XPASS=3; XFAIL=4
  STATUS_CODES={ #hash for convient programatic access
    :PASS  => PASS,
    :FAIL  => FAIL,
    :XPASS => XPASS,
    :XFAIL => XFAIL
  }

  class Parser
    LINE_FORMAT=/^.+\|.*\|(PASS|XFAIL)\|.*$/ 

    def initialize(input)
      @input = File.open(input)
    end

    def parse_line(line)
      line.rstrip! #get rid of trailing whitespace
      elems = line.split("|")
      td= {
      :path      =>   elems[0],
      :args      =>   elems[1],
      :expected  =>   elems[2],
      :comment   =>   elems[3] || "",
      }
      td
    end

    def parse()
      tds = []; tid = 1
      @input.each do |line|
        #path|chow_args|expected_result|comment
        if line =~ /^\s+$|^$|^#/ then
          #blank line or comment
        else
          if line =~ LINE_FORMAT then
            td = parse_line(line)
            if block_given? then yield td 
            else tds << TestDescription.new(tid,td) end
            tid += 1
          else
            puts "BAD LINE: "+line
          end
        end
      end
      tds
    end
  end

  class TestDescription
    attr_reader :test_id
    attr_accessor :result

    def initialize(id,vals)
      @test_id = id
      @path = vals[:path]
      @args = vals[:args]
      @expected = vals[:expected]
      @comment = vals[:comment]

      @expected_code = STATUS_CODES[@expected.to_sym]
    end

    def expected
      @expected_code
    end

    def cmd
      "rt --chow-args=\"#{@args}\" --test=\"#{@path}\" --exit-on-failure"
    end

    def to_s
      "#{@test_id}|#{@path}|#{@args}|#{@expected}|#{@comment}"
    end

    def result_s
      "#{@test_id}|#{@path}|#{@args}|#{@comment}  --- #{@result.to_s.upcase}"
    end
  end

  class Runner
    def initialize(tests, output=nil, logfile=nil)
      @tests = tests
      @output = 
        if output then File.open(output, "w+") else $stdout end
      @logger =
        if logfile then File.open(logfile, "w+") else $stderr end
    end

    class Stats
      attr_accessor :pass,:fail,:xpass,:xfail,:count
      attr_accessor :failures, :results, :successes

      def initialize
        @pass = @fail = @xpass = @xfail = @count = 0
        @results  = []
        @failures = []
        @successes= []
      end

      def record(test,type)
       @count += 1
       val = send("#{type}")
       send("#{type}=", val+1) 

       test.result = type
       @results << test
       if type == :fail || type == :xfail then
        @failures << test
       elsif type == :pass || type == :xpass then
        @successes << test
       end
      end
    end

    def run
      log "STARTING TEST RUN"
      starttime = Time.now
      log starttime
      stats = Stats.new

      tms = Benchmark.measure do
      @tests.each do |t|
        log "**** Running Test #{t.test_id} ****"
        output = `#{t.cmd}`
        status = $?.exitstatus
        log "test script exited with status: "+status.to_s

        test_status = 
        case status 
          when EXIT_SUCCESS 
            if t.expected == PASS then
              :pass
            elsif t.expected == XFAIL then
              :xpass
            else
              raise "programmer error"
            end
          when EXIT_FAILURE 
            if t.expected == PASS then
              :fail
            elsif t.expected == XFAIL then
              :xfail
            else
              raise "programmer error"
            end
          else 
            :unresolved
            raise "oh shit, bad stuff"
        end

        stats.record(t,test_status)
        log output
        log "**** TEST FINISHED WITH STATUS: #{test_status.to_s} ****"
      end#tests.each
      end#benchmark
      log ("tests completed in %.0f minutes %.2f seconds" % 
      [(tms.real/60), ((tms.real) % 60)])

      print_summary(stats, tms, starttime)
      @output.close
    end

    def print_summary(stats, tms, starttime)
      puts "*********************************************************"
      puts "                      SUMMARY                            "
      puts "*********************************************************"
      puts "Completed #{stats.count.to_i} tests"
      [:pass, :fail, :xpass, :xfail].each do |r|
        if stats.send(r) > 0 then
          puts "#{r.to_s.upcase}: #{stats.send(r)}"
        end
      end
      puts ""
      puts "Start time: #{starttime}"
      puts ("tests completed in %.0f minutes %.2f seconds" % 
                  [(tms.real/60), ((tms.real) % 60)])
      if not stats.failures.empty? then
        puts "---- FAILURES ----"
        stats.failures.each do |t|
          puts t.result_s
        end
      end

      if not stats.successes.empty? then
        puts "---- SUCCESSES ----"
        stats.successes.each do |t|
          puts t.result_s
        end
      end
    end


    def log(msg)
      @logger.puts(msg)
    end

    def puts(str)
      @output.puts str
    end
  end
end

#run this as a script
if __FILE__ == $0 then
  #parse options
  options = {}
  options[:logfile] = nil
  options[:output] = nil
  options[:testfiles] = []
  options[:mailto] = nil

  opts = 
  OptionParser.new do |opt|
    opt.banner = 
      "usage: regression_test [options] -f<file> [-f<file2> ...] \n"
      "Run regression tests for chow allocator"
    opt.separator ""
    #arguments passed to the chow allocator
    opt.on("-f TEST","--file TEST", "Use tests from file") {|t|
      options[:testfiles] << t
    }
    opt.on("-l=LOG_FILE", "--logfile=LOG_FILE",
        "Set the log file for the tests") {|f|
      options[:logfile] = f
    }
    opt.on("-o=OUTPUT_FILE", "--output=LOG_FILE",
        "Set the output file for the tests") {|f|
      options[:output] = f
    }
    opt.on("-m=MAIL_ADDRESS", "--mail-to=MAIL_ADDRESS",
        "Mail the output results to address") {|m|
      options[:mailto] = m
    }
  end
  begin
    opts.parse!
  rescue 
    puts opts.help
    exit
  end

  #check for required arguments
  if options[:testfiles].empty? then
    $stderr.puts "must have at least one -f flag"
    $stderr.puts opts.help
    exit
  end

  tests = []
  options[:testfiles].each do |f|
    tests.concat( RegressionTest::Parser.new(f).parse)
  end
  RegressionTest::Runner.new(tests,options[:output],options[:logfile]).run
end




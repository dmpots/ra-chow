#!/bin/env ruby
###############################################################
# utility to process the output of c2i instruction counts
#
# usage:
# ctest /path/to/test_dir -I -i -"{your_pass}"
#   --> generates a test_dir/ dir in the cwd
# st.rb test
#   --> gathers and prints out the statistics
#
###############################################################

def usage
  puts "usage: st basedir cvs_file"
  exit(1)
end

$stdir = ARGV[0]
$cvs_file = ARGV[1]
$stdir   || usage()
$cvs_file|| usage()

#print file
def cat(fname)
  puts File.open(fname).read()
end

#sum instruction count
def sum(fname)
  counts = 
  File.readlines(fname).map do |line|
    if line =~ /^\w/ then
      line.split(" ").last.to_i 
    else
      0
    end
  end
  counts.inject(0) {|sum, cnt| sum + cnt}
end

def dump_csv(header, values)
  File.open($cvs_file, "w") {|csv|
    csv.print(header.join(",")+"\n")
    csv.print(values.join(",")+"\n")
  }
end


gtotal = 0
totals = []
tests = []
stfiles = File.join($stdir,"**","*.stats")
Dir.glob(stfiles).each do |f|
  test = File.dirname(f).split("/").last
  puts "--------------------------------"
  puts  test
  puts "--------------------------------"
  cat f

  ftotal = sum f
  gtotal += ftotal
  puts "  TOTAL: #{ftotal}"

  totals << ftotal
  tests << test
end
puts "GRAND TOTAL: #{gtotal}"

dump_csv(tests, totals)




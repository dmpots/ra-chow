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

require 'fileutils'

def usage
  puts "usage: st basedir cvs_file"
  exit(1)
end

$stdir = ARGV[0]
$csv_file = ARGV[1]
$stdir   || usage()
$csv_file|| usage()

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
  File.open($csv_file, "w") {|csv|
    csv.print(header.join(",")+"\n")
    csv.print(values.join(",")+"\n")
  }
end

def csv(testname, fname)
  rows = 
  File.readlines(fname).map do |line|
    if line =~ /^\w/ and line !~ /^procedure/ then
      cols = line.split(" ")
      fun = cols.first
      cnt = cols.last
      "#{fun},#{cnt}"
    end
  end
  rows.compact! #get rid of nils
  File.open($csv_file, "a") do |f|
    f.puts
    f.puts("#{testname}")
    f.puts rows.join("\n")
  end
end

#truncate csv file
FileUtils.rm_f($csv_file)

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
  csv(test,f)
  puts "  TOTAL: #{ftotal}"

  totals << ftotal
  tests << test
end
puts "GRAND TOTAL: #{gtotal}"
cat $csv_file

#dump_csv(tests, totals)




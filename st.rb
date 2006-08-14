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
  puts "usage: st [basedir]"
  exit(1)
end

stdir = ARGV[0]
stdir || usage()

#print file
def cat(fname)
  puts File.open(fname).read()
end

#sum instruction count
def sum(fname)
  counts = 
  File.readlines(fname).map do |line|
    line.split(" ").last.to_i
  end
  counts.inject(0) {|sum, cnt| sum + cnt}
end

gtotal = 0
stfiles = File.join(stdir,"**","*.stats")
Dir.glob(stfiles).each do |f|
  puts "--------------------------------"
  puts File.dirname(f).split("/").last
  puts "--------------------------------"
  cat f

  ftotal = sum f
  gtotal += ftotal
  puts "  TOTAL: #{ftotal}"
end
puts "GRAND TOTAL: #{gtotal}"




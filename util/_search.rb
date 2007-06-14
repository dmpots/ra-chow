#!/bin/env ruby

R=(32..32)
B=(2..10)
C=(0..3)
S=(0..1)
L=['0,0','0,2','0,4','0,8']
M=['-m', '-e', '  ']
#E=['-e', '  ']
Z=['-z', '  ']
T=['-t', '  ']
G=['-g', '  ']

spills = {}
R.each do |r|
B.each do |b|
C.each do |c|
S.each do |s|
M.each do |m|
Z.each do |z|
T.each do |t|
L.each do |l|
G.each do |g|
  opts="-r#{r} -b#{b} -c#{c} -s#{s} -l#{l} #{m} #{z} #{t} #{g}"
  rez = `cat 1.i | chow #{opts} 2>/dev/null | dead | clean | grep SPI | wc -l`
  rez.rstrip!.lstrip!
#  spills[opts] = rez
  printf "%10d ==> %s\n", rez, opts
  $stdout.flush
end
end
end
end
end
end
end
end
end

#sorted = spills.sort_by {|k,v| v}.map do |k,v| "#{k} ==> #{v}" end
#puts sorted


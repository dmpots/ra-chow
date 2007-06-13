
funcs = %w(
aclear
arret
bcndX
bilan
bilsla
blts
buts
cardeb
celbndX
coeray
colbur
dcoera
ddeflu
debflu
debico
decomp
denptX
densX
densyX
deseco
diagnsX
drepvi
drigl
dyeh
ecrdX
ecwrX
efill
energyX
erhs
error
exact
fehl
fftbX
fftfX
fieldX
fmin
fmtgen
fmtset
fpppp
gamgen
genbX
genprbX
getbX
heat
hmoy
ihbtr
ilsw
inideb
iniset
inisla
initX
inithx
injchkX
integr
inter
intowp
jacld
jacu
jobtimX
l2norm
lasdenX
laspowX
lclear
lissag
numbX
orgpar
parmvrX
paroi
pastem
pdiagX
pintgr
prophy
putbX
radb2X
radb4X
radb5X
radf2X
radf4X
radf5X
ranfX
repvid
rfftbX
rfftb1X
rfftfX
rfftf1X
rfftiX
rffti1X
rhs
rkf45
rkfs
saturr
saxpy
setbX
setbv
setinjX
setiv
seval
sgemm
sgemv
si
sigma
slv2xyX
smoothX
solv2yX
solve
sortie
spline
ssor
subb
supp
svd
transX
twldrv
urand
verify
vgjyeh
vslv1pX
x21y21
yeh
zeroin
)

funcs.each do |f|
  if not File.exists?(File.join("iloc",f)+".i") then
    puts "non-exist: #{f}"; exit
  end
end
#Dir["fmin.i"].each do |f|
funcs.each do |f|
  puts f
  ff = "_1.i"
  `prep iloc/#{f}.i | clean > #{ff}`
  s = `chow -g #{ff}`
  t0 =  s.grep(/Total/)[0]
  t1 =  s.grep(/Total/)[1]
  puts t0
  puts t1
  s = `chow -b5 -g #{ff}`
  t2 = s.grep(/Total/)[1]
  puts t2

  tot =  /.*Total:\s+/
  t0.gsub!(tot,""); t1.gsub!(tot,""); t2.gsub!(tot,"")
  if t1 != t2  || t0 != t1 then 
    #puts "!!MISMATCH!! (#{f})\n#{t0}#{t1}#{t2}" 
    puts "!!iloc/#{f}.i"
  end
  $stdout.flush
end


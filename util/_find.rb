cid1 = ARGV[0]
cid2 = ARGV[1]

staticSizes = {}
sizes = `wc -l iloc/*.i | sort | sed 's#iloc/##' | sed 's#^[ |\t]*##'`
sizes.split("\n").each do |line|
  n,f = line.split(/\s+/)
  f.gsub!(".i","")
  f.gsub!("X","")
  staticSizes[f] = n.to_i
end

TSQL="
select t1.id,t2.id 
from ra_benchmarks t1, ra_benchmarks t2 
where 
  t1.configuration_id = #{cid1} and 
  t2.configuration_id = #{cid2} and 
  t1.program = t2.program;
"

FSQL="
select t1.function_name f1 
from timings t1, timings t2 
where
  t1.ra_benchmark_id = %d and
  t2.ra_benchmark_id = %d and 
  t1.instruction_count != t2.instruction_count and
  t1.function_name = t2.function_name;
"
CHOW_DB="~/research/ra-chow-analysis/db/chow.db"

funcs = []
ids = `sqlite3 ~/research/ra-chow-analysis/db/chow.db '#{TSQL}'`
ids.split("\n").each do |line|
  id1,id2 = line.split("|")
  #puts "sqlite3 ~/research/ra-chow-analysis/db/chow.db '#{FSQL % [id1,id2]}"
  mismatch = `sqlite3 #{CHOW_DB} '#{FSQL % [id1,id2]}'`
  mismatch.split("\n").each {|fname| funcs << fname}
end

used = {}
funcs.each {|f| used[f] = staticSizes[f] if staticSizes[f]}
used.sort_by {|k,v| v}.each do |a|
  puts "#{a[0]} -- #{a[1]}"
end



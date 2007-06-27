
def diff(v1,v2)
  return "-" if ((v1.nil? || v1 == "") || (v2.nil? || v2 == ""))
  v = ((v1.tr(",","").to_f / v2.tr(",","").to_f) - 1.0) * 100.0
  v *= -1.0 unless v == 0.0
  sprintf "%.2f", v
end

while gets do
  if $_ =~ /HEAD/ then
    h = chomp.split("|")[1]
    puts "%"
    puts "% #{h.upcase}"
    puts "%"
    puts "\\multicolumn{8}{|l|}{#{h}}\\\\"
    puts "\\hline"
  else
    fun,cS,rS,cD,rD,oldDiff = chomp.split("|")
    printf "\\texttt{%s}&%s&%s&%s&%s&%s&%s&%.2f \\\\\n",
      fun,cS,rS,diff(cS,rS),cD,rD,diff(cD,rD),oldDiff.to_f
    puts "\\hline"
  end
end


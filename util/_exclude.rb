#!/bin/env ruby
require 'yaml'

minval = ARGV[0].to_i

mins = YAML::load_file("_MINREGS_RA")
mins.keys.each do|f|
  if mins[f].to_i > minval then puts f end
end


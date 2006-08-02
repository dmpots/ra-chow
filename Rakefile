task :default => [:scratch, :runtest]

class TestFile
  attr_reader :chow
  attr_reader :base
  attr_reader :unit

  def initialize(basename)
    @basename = basename
    @chow = VersionedFile.new("scratch/#{basename}_chow")
    @base = VersionedFile.new("scratch/#{basename}_base")
    @unit = VersionedFile.new("unit_test/#{basename}")
  end

  class VersionedFile
    def initialize(name)
      @name = name
    end
    def i
      @name+".i"
    end
    def c
      @name+".c"
    end
    def exe
      @name+".exe"
    end
  end
end

CHOW_PARAMS = "-r2 -b3"
TR_PRINTF = "sed 's/printf_/printf/g'" 

#list of test files
#test_files = FileList["simp", "nospill"]
test_files = ["simp", "nospill", "loop"].map {|f| TestFile.new(f)}
#scratch_files = test_files.map {|f| "scratch/#{f}"} 
#scratch_files = test_files.map {|f| "scratch/#{f}"} 

#baseline files
#baseline_files = scratch_files.map {|f| TestFile.new(f+"_base")}
#baseline_exe = baseline_files.map {|f| f.exe}
#baseline_i = baseline_files.map {|f| f.i}
#baseline_c = baseline_files.map {|f| f.c}
#
##files processed with chow allocator
#chow_files =  scratch_files.map {|f| TestFile.new(f+"_chow")}
#chow_exe = chow_files.map {|f| f.exe}
#chow_i = chow_files.map {|f| f.i}
#chow_c = chow_files.map {|f| f.c}
#
task :chow_i => "scratch" do
  test_files.each {|f|
    cp "#{f.unit.i}", "#{f.chow.i}"
  }
end

task :baseline_i => "scratch" do
  test_files.each {|f|
    cp "#{f.unit.i}", "#{f.base.i}"
  }
end

task :chow_c => :chow_i do
  test_files.each {|tf|
    sh "cat #{tf.chow.i} | chow #{CHOW_PARAMS} | i2c | #{TR_PRINTF} > #{tf.chow.c}"
  }
end

task :baseline_c => :baseline_i do
  test_files.each {|tf|
    sh "cat #{tf.base.i} | i2c | #{TR_PRINTF} > #{tf.base.c}"
  }
end

task :chowtest => :testexe do
  test_files.each  do |f|
    sh "./#{f.chow.exe}"
  end
end

task :runtest => :testexe do |t|
  test_files.each  do |f|
    sh "./#{f.chow.exe}"
    sh "./#{f.base.exe}"
  end
end

#task :testexe => [:chow_exe, :baseline_exe]
task :testexe => [:chow_c, :baseline_c] do
  test_files.each {|tf|
    sh "gcc -o #{tf.chow.exe} #{tf.chow.c}"
    sh "gcc -o #{tf.base.exe} #{tf.base.c}"
  }
end

directory "scratch"
file "scratch" do
  cp Dir["unit_test/*.i"], "scratch"
end


task :clean do
  sh "rm -rf scratch"
end

rule '.exe' => ['.o'] do |t|
  sh "gcc -o #{t.name} #{t.source}"
end

rule '.o' => ['.c'] do |t|
  sh "gcc #{t.source} -c -o #{t.name}"
end



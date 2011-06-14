require 'rubygems'
require 'test/unit'
require 'shoulda'

$LOAD_PATH.unshift(File.join(File.dirname(__FILE__), '..', 'lib'))
$LOAD_PATH.unshift(File.join(File.dirname(__FILE__), '..', 'ext', 'bert', 'c'))

load 'bert.rb'

puts "Using Decode #{BERT::Decode.impl} implementation."
puts "Using Encode #{BERT::Encode.impl} implementation."

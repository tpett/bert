
require 'stringio'

$:.unshift File.join(File.dirname(__FILE__), *%w[.. ext])

require 'bert/bert'
require 'bert/types'

begin
  # try to load the C extension
  require 'bert/c/decode/decode'
rescue LoadError
  # fall back on the pure ruby version
  require 'bert/decode'
end

begin
  require 'bert/c/encode/encode'
rescue LoadError
  require 'bert/encode'
end

require 'bert/encoder'
require 'bert/decoder'

# Global method for specifying that an array should be encoded as a tuple.
def t
  BERT::Tuple
end

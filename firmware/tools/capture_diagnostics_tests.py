import struct
import unittest
from capture_diagnostics import decode,WORDS,NAMES
class DiagnosticsTests(unittest.TestCase):
 def test_decode(self):
  words=[0]*WORDS;words[0]=1;words[7]=len(NAMES);words[5]=1
  words[8:14]=[2,6000,1,4000,100,200]
  base=8+len(NAMES)*6
  words[base+3]=17
  words[base+65:base+70]=[3072,17,100,200,0]
  data=decode(struct.pack('<'+'I'*WORDS,*words))
  self.assertEqual(data['timings']['authentication']['total_us'],(1<<32)+6000)
  self.assertEqual(data['read_regions'][0]['first_lba'],3072)
  self.assertEqual(data['recent_runs'][0]['requests'],17)
 def test_reject(self):
  with self.assertRaises(ValueError):decode(b'bad')
  with self.assertRaises(ValueError):decode(bytes(WORDS*4))
if __name__=='__main__':unittest.main()

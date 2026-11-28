#!/usr/bin/env python3
"""
Round-trip compression/decompression tests for compress_ha_discovery.py.

Verifies that JSONL data compressed by compress_ha_discovery.py can be
decompressed back to the original, and that edge cases (empty, large,
chunk-boundary) are handled correctly.
"""

import json
import sys
import zlib
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "ha_discovery" / "generators"))
import compress_ha_discovery as compress

class TestCompressDecompressRoundTrip(unittest.TestCase):
    """Test that compression followed by decompression recovers the original."""

    def _decompress_chunks(self, chunks):
        """Decompress a list of (compressed_data, size) tuples back to original JSONL."""
        result = b''
        for chunk_data, _ in chunks:
            result += zlib.decompress(chunk_data)
        return result.decode('utf-8')

    def test_simple_jsonl_round_trip(self):
        """Small JSONL compresses and decompresses correctly."""
        jsonl = '{"i":"0001","n":"Test","d":"sensor"}\n{"i":"0002","n":"Temp","d":"sensor"}\n'
        chunks = compress.split_into_chunks(jsonl.encode('utf-8'))
        decompressed = self._decompress_chunks(chunks)
        self.assertEqual(jsonl, decompressed)

    def test_empty_jsonl(self):
        """Empty JSONL produces no chunks."""
        chunks = compress.split_into_chunks(b'')
        self.assertEqual(len(chunks), 0)

    def test_single_line_jsonl(self):
        """Single line JSONL compresses and decompresses correctly."""
        jsonl = '{"i":"0001","n":"Test","d":"sensor"}\n'
        chunks = compress.split_into_chunks(jsonl.encode('utf-8'))
        decompressed = self._decompress_chunks(chunks)
        self.assertEqual(jsonl, decompressed)

    def test_large_jsonl_round_trip(self):
        """Large multi-chunk JSONL compresses and decompresses correctly."""
        lines = []
        for i in range(1, 501):
            lines.append(json.dumps({
                'i': f'{i:04X}',
                'n': f'Entity {i}',
                'd': 'sensor',
                'vt': '{{ value | int }}',
                'u': '°C'
            }))
        jsonl = '\n'.join(lines) + '\n'
        chunks = compress.split_into_chunks(jsonl.encode('utf-8'))
        decompressed = self._decompress_chunks(chunks)
        self.assertEqual(jsonl, decompressed)
        self.assertGreater(len(chunks), 1)

    def test_chunk_boundaries(self):
        """JSONL with lines near chunk boundary compresses correctly."""
        long_value = 'x' * 10000
        jsonl = json.dumps({'i': '0001', 'n': 'Long', 'd': 'sensor', 'vt': long_value}) + '\n'
        chunks = compress.split_into_chunks(jsonl.encode('utf-8'))
        decompressed = self._decompress_chunks(chunks)
        self.assertEqual(jsonl, decompressed)

    def test_special_characters_in_jsonl(self):
        """JSONL with special characters compresses and decompresses correctly."""
        jsonl = '{"i":"0001","n":"Test \\\"quoted\\\"","d":"sensor"}\n'
        chunks = compress.split_into_chunks(jsonl.encode('utf-8'))
        decompressed = self._decompress_chunks(chunks)
        self.assertEqual(jsonl, decompressed)

    def test_unicode_in_jsonl(self):
        """JSONL with unicode characters compresses and decompresses correctly."""
        jsonl = '{"i":"0001","n":"Tëst","d":"sensor"}\n'
        chunks = compress.split_into_chunks(jsonl.encode('utf-8'))
        decompressed = self._decompress_chunks(chunks)
        self.assertEqual(jsonl, decompressed)

    def test_compressed_data_is_smaller(self):
        """Compressed data should be smaller than original for typical JSONL."""
        lines = []
        for i in range(1, 101):
            lines.append(json.dumps({
                'i': f'{i:04X}',
                'n': f'Entity {i}',
                'd': 'sensor',
                'vt': '{{ value | int }}',
                'u': '°C'
            }))
        jsonl = '\n'.join(lines) + '\n'
        chunks = compress.split_into_chunks(jsonl.encode('utf-8'))
        total_compressed = sum(len(c[0]) for c in chunks)
        self.assertLess(total_compressed, len(jsonl.encode('utf-8')))

class TestCompressChunk(unittest.TestCase):
    """Test the compress_chunk function directly."""

    def test_compress_chunk_produces_valid_zlib(self):
        """compress_chunk output can be decompressed with zlib."""
        data = b'{"i":"0001","n":"Test","d":"sensor"}\n'
        compressed = compress.compress_chunk(data)
        decompressed = zlib.decompress(compressed)
        self.assertEqual(data, decompressed)

    def test_compress_chunk_is_compressed(self):
        """compress_chunk output is smaller than input for repetitive data."""
        data = b'{"i":"0001","n":"Test","d":"sensor"}\n' * 100
        compressed = compress.compress_chunk(data)
        self.assertLess(len(compressed), len(data))

    def test_compress_chunk_empty(self):
        """compress_chunk handles empty input."""
        compressed = compress.compress_chunk(b'')
        decompressed = zlib.decompress(compressed)
        self.assertEqual(b'', decompressed)

class TestSplitIntoChunks(unittest.TestCase):
    """Test split_into_chunks behavior."""

    def test_returns_list_of_tuples(self):
        """split_into_chunks returns list of (bytes, int) tuples."""
        jsonl = b'{"i":"0001","n":"Test","d":"sensor"}\n'
        chunks = compress.split_into_chunks(jsonl)
        self.assertIsInstance(chunks, list)
        for chunk_data, chunk_size in chunks:
            self.assertIsInstance(chunk_data, bytes)
            self.assertIsInstance(chunk_size, int)
            self.assertGreater(chunk_size, 0)

    def test_chunk_decompressed_size_matches(self):
        """Each chunk's reported size matches the decompressed data."""
        lines = []
        for i in range(1, 51):
            lines.append(json.dumps({
                'i': f'{i:04X}',
                'n': f'Entity {i}',
                'd': 'sensor',
            }))
        jsonl = ('\n'.join(lines) + '\n').encode('utf-8')
        chunks = compress.split_into_chunks(jsonl)
        for chunk_data, reported_size in chunks:
            decompressed = zlib.decompress(chunk_data)
            self.assertEqual(reported_size, len(decompressed))

    def test_all_chunks_decompress_to_original(self):
        """Concatenating all decompressed chunks recovers the original."""
        lines = []
        for i in range(1, 51):
            lines.append(json.dumps({
                'i': f'{i:04X}',
                'n': f'Entity {i}',
                'd': 'sensor',
            }))
        jsonl = ('\n'.join(lines) + '\n').encode('utf-8')
        chunks = compress.split_into_chunks(jsonl)
        result = b''
        for chunk_data, _ in chunks:
            result += zlib.decompress(chunk_data)
        self.assertEqual(jsonl, result)

    def test_empty_input_produces_no_chunks(self):
        """Empty input produces no chunks."""
        chunks = compress.split_into_chunks(b'')
        self.assertEqual(len(chunks), 0)

    def test_single_line_produces_one_chunk(self):
        """A single line produces exactly one chunk."""
        jsonl = b'{"i":"0001","n":"Test","d":"sensor"}\n'
        chunks = compress.split_into_chunks(jsonl)
        self.assertEqual(len(chunks), 1)

    def test_trailing_newline_is_preserved(self):
        """Trailing newlines in JSONL are preserved through compression."""
        jsonl = b'{"i":"0001","n":"Test","d":"sensor"}\n'
        chunks = compress.split_into_chunks(jsonl)
        result = b''
        for chunk_data, _ in chunks:
            result += zlib.decompress(chunk_data)
        self.assertTrue(result.endswith(b'\n'))

class TestBytesToCArray(unittest.TestCase):
    """Test bytes_to_c_array formatting."""

    def test_produces_valid_c_hex(self):
        """bytes_to_c_array produces valid C hex literals."""
        data = b'\x00\x01\x02\xff'
        result = compress.bytes_to_c_array(data)
        self.assertIn('0x00', result)
        self.assertIn('0x01', result)
        self.assertIn('0x02', result)
        self.assertIn('0xff', result)

    def test_empty_produces_empty(self):
        """Empty data produces empty string."""
        result = compress.bytes_to_c_array(b'')
        self.assertEqual(result, '')

    def test_indentation(self):
        """bytes_to_c_array respects indent parameter."""
        data = b'\x00\x01'
        result = compress.bytes_to_c_array(data, indent=4)
        lines = result.split('\n')
        for line in lines:
            if line:
                self.assertTrue(line.startswith('    '), f"Line '{line}' doesn't start with 4 spaces")

class TestCategoryToCppName(unittest.TestCase):
    """Test category_to_cpp_name."""

    def test_lowercase(self):
        """category_to_cpp_name returns lowercase."""
        self.assertEqual(compress.category_to_cpp_name('Washer'), 'washer')
        self.assertEqual(compress.category_to_cpp_name('DRYER'), 'dryer')

    def test_empty(self):
        """Empty category returns empty."""
        self.assertEqual(compress.category_to_cpp_name(''), '')

class TestCompressEdgeCases(unittest.TestCase):
    """Test edge cases in compression."""

    def test_newline_only(self):
        """JSONL with only a newline produces no chunks (empty lines skipped)."""
        chunks = compress.split_into_chunks(b'\n')
        self.assertEqual(len(chunks), 0)

    def test_very_long_single_line(self):
        """Very long single line JSONL compresses correctly."""
        long_value = 'x' * 50000
        jsonl = (json.dumps({'i': '0001', 'n': 'Long', 'd': 'sensor', 'vt': long_value}) + '\n').encode('utf-8')
        chunks = compress.split_into_chunks(jsonl)
        result = b''
        for chunk_data, _ in chunks:
            result += zlib.decompress(chunk_data)
        self.assertEqual(jsonl, result)

    def test_many_small_lines(self):
        """Many small lines compress correctly across multiple chunks."""
        lines = [json.dumps({'i': f'{i:04X}', 'n': f'N{i}', 'd': 'sensor'}) for i in range(1, 2001)]
        jsonl = ('\n'.join(lines) + '\n').encode('utf-8')
        chunks = compress.split_into_chunks(jsonl)
        result = b''
        for chunk_data, _ in chunks:
            result += zlib.decompress(chunk_data)
        self.assertEqual(jsonl, result)

    def test_all_real_chunks_fit_in_decomp_buffer(self):
        """Verify all actual JSONL chunks decompress to <= HA_DISCOVERY_DECOMP_BUF_SIZE (18432)."""
        import glob as glob_mod
        DECOMP_BUF_SIZE = 18432
        jsonl_dir = Path(__file__).parent.parent / "ha_discovery"
        for jsonl_path in sorted(jsonl_dir.glob("*.jsonl")):
            raw = jsonl_path.read_bytes()
            chunks = compress.split_into_chunks(raw)
            for i, (chunk_data, reported_size) in enumerate(chunks):
                self.assertLessEqual(
                    reported_size, DECOMP_BUF_SIZE,
                    f"{jsonl_path.name} chunk {i}: decompressed size {reported_size} exceeds buffer {DECOMP_BUF_SIZE}"
                )

if __name__ == '__main__':
    unittest.main()

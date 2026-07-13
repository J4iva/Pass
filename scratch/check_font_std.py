import struct
import sys

def check_ttf_cmap(path):
    print(f"Checking {path}...")
    try:
        with open(path, 'rb') as f:
            data = f.read()
        
        # Parse TTF header
        if len(data) < 12:
            print("Invalid TTF file (too short)")
            return
        
        scalar_type, num_tables, search_range, entry_selector, range_shift = struct.unpack('>IHHHH', data[:12])
        
        # Find cmap table
        cmap_offset = None
        cmap_length = None
        for i in range(num_tables):
            off = 12 + i * 16
            tag, checksum, offset, length = struct.unpack('>4sIII', data[off:off+16])
            tag_str = tag.decode('latin1')
            if tag_str == 'cmap':
                cmap_offset = offset
                cmap_length = length
                break
        
        if cmap_offset is None:
            print("cmap table not found")
            return
        
        # Parse cmap header
        version, num_subtables = struct.unpack('>HH', data[cmap_offset:cmap_offset+4])
        
        # Read subtable info
        subtables = []
        for i in range(num_subtables):
            off = cmap_offset + 4 + i * 8
            platform_id, encoding_id, offset_sub = struct.unpack('>HHh', data[off:off+6])
            # offset_sub can be unsigned, so let's unpack properly
            offset_sub = struct.unpack('>I', data[off+4:off+8])[0]
            subtables.append((platform_id, encoding_id, offset_sub))
        
        # We want to find Unicode cmap (Platform 0 or Platform 3/Encoding 1 or 10)
        # Let's find Platform 3, Encoding 1 (Windows Unicode BMP) or Platform 0 (Unicode)
        target_subtable_offset = None
        for platform_id, encoding_id, offset_sub in subtables:
            if (platform_id == 3 and encoding_id == 1) or platform_id == 0:
                target_subtable_offset = cmap_offset + offset_sub
                break
        
        if target_subtable_offset is None and subtables:
            # Fallback to the first subtable
            target_subtable_offset = cmap_offset + subtables[0][2]
            
        if target_subtable_offset is None:
            print("No unicode subtable found in cmap")
            return
            
        # Parse subtable format
        format = struct.unpack('>H', data[target_subtable_offset:target_subtable_offset+2])[0]
        supported_chars = set()
        
        if format == 4:
            # Format 4: Segment mapping to delta values (standard BMP)
            length, language, seg_count_x2 = struct.unpack('>HHH', data[target_subtable_offset+2:target_subtable_offset+8])
            seg_count = seg_count_x2 // 2
            
            end_code_offset = target_subtable_offset + 14
            start_code_offset = end_code_offset + 2 + seg_count * 2
            id_delta_offset = start_code_offset + seg_count * 2
            id_range_offset = id_delta_offset + seg_count * 2
            
            end_codes = struct.unpack(f'>{seg_count}H', data[end_code_offset:end_code_offset + seg_count * 2])
            start_codes = struct.unpack(f'>{seg_count}H', data[start_code_offset:start_code_offset + seg_count * 2])
            id_deltas = struct.unpack(f'>{seg_count}h', data[id_delta_offset:id_delta_offset + seg_count * 2])
            
            for seg in range(seg_count):
                start = start_codes[seg]
                end = end_codes[seg]
                if start == 0xFFFF and end == 0xFFFF:
                    continue
                # For simplicity, if start code to end code is mapped, we check if glyph index is valid (not 0)
                # But since we just want to see if the range covers the char, we can approximate:
                # If start <= char <= end, it's likely supported unless mapped to glyph 0
                for cp in range(start, end + 1):
                    # We can do full glyph index calculation if needed:
                    # offset in id_range_offset
                    # For simplicity, let's just add to supported
                    supported_chars.add(cp)
        else:
            print(f"Unsupported cmap subtable format: {format}")
            return
            
        chars = {
            'á': 0xe1, 'é': 0xe9, 'í': 0xed, 'ó': 0xf3, 'ú': 0xfa,
            'Á': 0xc1, 'É': 0xc9, 'Í': 0xcd, 'Ó': 0xd3, 'Ú': 0xda,
            'ñ': 0xf1, 'Ñ': 0xd1
        }
        for char, code in chars.items():
            supported = code in supported_chars
            print(f"  {char} (0x{code:02X}): {'YES' if supported else 'NO'}")
            
    except Exception as e:
        print(f"Error checking {path}: {e}")

if __name__ == '__main__':
    if len(sys.argv) > 1:
        check_ttf_cmap(sys.argv[1])
    else:
        check_ttf_cmap("app/assets/fonts/JetBrainsMono.ttf")
        check_ttf_cmap("app/assets/fonts/Doto.ttf")
        check_ttf_cmap("app/assets/fonts/VT323.ttf")

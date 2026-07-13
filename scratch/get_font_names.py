import struct
import sys

def get_font_family_name(path):
    print(f"Reading family name for {path}...")
    try:
        with open(path, 'rb') as f:
            data = f.read()
        
        # Parse TTF header
        if len(data) < 12:
            return "Invalid TTF"
        
        scalar_type, num_tables, search_range, entry_selector, range_shift = struct.unpack('>IHHHH', data[:12])
        
        # Find 'name' table
        name_offset = None
        for i in range(num_tables):
            off = 12 + i * 16
            tag, checksum, offset, length = struct.unpack('>4sIII', data[off:off+16])
            tag_str = tag.decode('latin1')
            if tag_str == 'name':
                name_offset = offset
                break
        
        if name_offset is None:
            return "name table not found"
        
        # Parse 'name' table header
        format, count, string_offset = struct.unpack('>HHH', data[name_offset:name_offset+6])
        
        # Parse name records
        for i in range(count):
            off = name_offset + 6 + i * 12
            platform_id, encoding_id, language_id, name_id, length, offset_str = struct.unpack('>HHHHHH', data[off:off+12])
            
            # name_id 1 is Font Family Name, name_id 4 is Full Font Name
            if name_id == 1:
                start = name_offset + string_offset + offset_str
                end = start + length
                name_bytes = data[start:end]
                
                # Decode depending on platform/encoding
                if platform_id == 3 or platform_id == 0:  # Unicode (Windows/ISO)
                    try:
                        name = name_bytes.decode('utf-16-be')
                    except Exception:
                        name = name_bytes.decode('latin1')
                else:
                    name = name_bytes.decode('latin1')
                
                print(f"  Platform {platform_id}, Encoding {encoding_id}, Lang {language_id}: {name}")
                
    except Exception as e:
        print(f"Error: {e}")

if __name__ == '__main__':
    get_font_family_name("app/assets/fonts/JetBrainsMono.ttf")
    get_font_family_name("app/assets/fonts/Doto.ttf")
    get_font_family_name("app/assets/fonts/VT323.ttf")

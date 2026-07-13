import sys
from fontTools.ttLib import TTFont

def check_font(path):
    print(f"Checking {path}...")
    try:
        font = TTFont(path)
        cmap = font.getBestCmap()
        # Accented characters we care about
        chars = {
            'á': 0xe1, 'é': 0xe9, 'í': 0xed, 'ó': 0xf3, 'ú': 0xfa,
            'Á': 0xc1, 'É': 0xc9, 'Í': 0xcd, 'Ó': 0xd3, 'Ú': 0xda,
            'ñ': 0xf1, 'Ñ': 0xd1
        }
        for char, code in chars.items():
            supported = code in cmap
            print(f"  {char} (0x{code:02X}): {'YES' if supported else 'NO'} ({cmap.get(code, 'None')})")
    except Exception as e:
        print(f"Error checking {path}: {e}")

if __name__ == '__main__':
    if len(sys.argv) > 1:
        check_font(sys.argv[1])
    else:
        check_font("app/assets/fonts/JetBrainsMono.ttf")
        check_font("app/assets/fonts/Doto.ttf")
        check_font("app/assets/fonts/VT323.ttf")

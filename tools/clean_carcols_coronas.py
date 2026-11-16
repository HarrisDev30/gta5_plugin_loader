import re, os

def clean_carcols(cc_path):
    if not os.path.exists(cc_path):
        print(f"Not found: {cc_path}")
        return
    txt = open(cc_path, 'r', encoding='utf-8').read()

    def zero_coronas(m):
        tag = m.group(1)
        body = m.group(2)
        body = re.sub(r'<size\s+value="[^"]*"\s*/>', '<size value="0.000000" />', body)
        body = re.sub(r'<size_far\s+value="[^"]*"\s*/>', '<size_far value="0.000000" />', body)
        body = re.sub(r'<intensity\s+value="[^"]*"\s*/>', '<intensity value="0.000000" />', body)
        body = re.sub(r'<intensity_far\s+value="[^"]*"\s*/>', '<intensity_far value="0.000000" />', body)
        return f'<{tag}>{body}</{tag}>'

    txt = re.sub(r'<(tailLightCorona|tailLightMiddleCorona|headLightCorona|reversingLightCorona|frontIndicatorCorona|rearIndicatorCorona)>(.*?)</\1>', zero_coronas, txt, flags=re.DOTALL)
    open(cc_path, 'w', encoding='utf-8', newline='\r\n').write(txt)
    print(f"Zeroed coronas in {os.path.basename(cc_path)} successfully!")

clean_carcols(r'D:\ps5payload\clean_pc_27rev\common\data\carcols.meta')


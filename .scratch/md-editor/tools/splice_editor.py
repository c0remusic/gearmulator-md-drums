# Puts the output of gen_editor.py (topbar_<model>.rml.txt, editor_<model>.rml.txt, written in the
# current directory) into the two skins. Run from this folder after "python3 gen_editor.py md" and
# "python3 gen_editor.py mm". It replaces the text between the markers below, nothing else.
import os
HERE = os.path.dirname(os.path.abspath(__file__))
SK = os.path.normpath(os.path.join(HERE, "../../../source/elektron/md/mdJucePlugin/skins")) + "/"
for model, path in [("md", "mdDefault/mdDefault.rml"), ("mm", "mmSfx60/mmSfx60.rml")]:
    s = open(SK + path).read()
    topbar = open(f"topbar_{model}.rml.txt").read()
    editor = open(f"editor_{model}.rml.txt").read()
    a = s.index("\t\t<!-- ===== Top bar"); b = s.index("\t\t<!-- The front panel")
    s = s[:a] + topbar + s[b:]
    a = s.index("\t\t<!-- ===== Editor:"); b = s.index("\t</body>")
    s = s[:a] + editor + s[b:]
    open(SK + path, "w").write(s)
    print("updated", path)

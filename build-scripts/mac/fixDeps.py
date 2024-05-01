import os
import sys
import subprocess

def getDeps(lib):
    try:
        output = subprocess.check_output(["otool", "-L", lib])
        lines = str(output).split('\\n\\t')
    except:
        return []
    deps = []
    for dep in lines:
        if "/opt/" in dep or "/Cellar" in dep or "/Users" in dep or dep.startswith("lib") or not (dep.startswith("/") or dep.startswith("@executable_path")):
            deps.append(dep.split(' ')[0])
    return deps

def fixDeps(path):
    name = os.path.basename(path)
    deps = getDeps(path)
    print(deps)
    for dep in deps:
        dname = os.path.basename(dep)
        if dname == name:
            print("id change {}".format(dname))
            subprocess.check_output(['install_name_tool', '-id', '@executable_path/../Frameworks/' + name, path])
        else:
            if ".framework" in dep:
                namePart = dep.split(".framework")[0]
                fname = os.path.dirname(namePart)
                print("replace following {} {}".format(dep, dep.replace(fname, "@executable_path/../Frameworks/")))
                subprocess.check_output(['install_name_tool', '-change', dep, dep.replace(fname, "@executable_path/../Frameworks/"), path])
            else:
                if os.path.dirname(dep) == "":
                    subprocess.check_output(['install_name_tool', '-change', dep, '@executable_path/../Frameworks/' + dep, path])
                else:
                    subprocess.check_output(['install_name_tool', '-change', dep, dep.replace(os.path.dirname(dep), '@executable_path/../Frameworks/'), path])

for file in sys.argv[1:]:
    fixDeps(file)

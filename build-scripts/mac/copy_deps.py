#!/bin/exec python3
import subprocess
import os
import sys
from shutil import copyfile

def get_dependencies(lib):
    try:
        otool = subprocess.Popen(["otool", "-L", lib], stdout=subprocess.PIPE)
        grep = subprocess.Popen(["grep", "/opt[^:]*$"], stdin=otool.stdout, stdout=subprocess.PIPE)
        output = subprocess.check_output(["awk", "-F", " ", "{print $1}"], stdin=grep.stdout, encoding="UTF-8")
        lines = str(output).split('\n')
    except:
        return []
    deps = [dep for dep in lines if "b'" not in dep and len(dep) > 0]
    return deps

def copy_all(files, dest):
    copied = {}
    while len(files) > 0:
        file = files.pop()
        filename = os.path.basename(file)
        if filename not in copied:
            print(f"Copying file {file}")
            copyfile(file, os.path.join(dest, filename))
            files = files + get_dependencies(file)
        copied[filename] = True

copy_all(sys.argv[2:], sys.argv[1])

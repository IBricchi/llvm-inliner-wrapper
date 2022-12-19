#!/usr/bin/env python3

import sys
import os
import subprocess

class Command:
    def __init__(self):
        self.clang_path = "clang"

        script_dir = os.path.dirname(os.path.realpath(__file__))
        plugin_path = os.path.join(script_dir, "build", "plugin.so")
        self.arguments = [
            "-O3", "-g",
            "-fpass-plugin=" + plugin_path,
            "-mllvm", "--inline-priority-mode=top-down",
            "-mllvm", "--enable-module-inliner",
        ]
        self.extra_args = []
    
    def call(self, output_path, advice_path = None):
        if advice_path == None:
            os.environ["INLINE_ADVISOR_ADVICE_FILE"] = ""
        else:
            os.environ["INLINE_ADVISOR_ADVICE_FILE"] = advice_path
        
        command = " ".join([self.clang_path] + self.arguments + ["-o", output_path] + self.extra_args)
        print (command)

        p = subprocess.run(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return p.stdout.decode("utf-8"), p.stderr.decode("utf-8")

if(__name__ == "__main__"):
    command = Command()
    advice_path = None
    for i, a in enumerate(sys.argv[1:]):
        if a.startswith("--clang="):
            command.clang_path = a.split("=")[1]
        elif a.startswith("--advice="):
            advice_path = a.split("=")[1]
        elif a == "--dot-format":
            os.environ["INLINE_ADVISOR_DOT_FORMAT"] = "1"
        elif a == "--":
            command.extra_args = sys.argv[i+2:]
            break

    stdout, stderr = command.call(advice_path)

    if stderr != "":
        print("Output: ", file=sys.stdout)
        print(stdout, file=sys.stdout)
        print("Error: ", file=sys.stderr)
        print(stderr, file=sys.stderr)
    else:
        print(stdout, file=sys.stdout)

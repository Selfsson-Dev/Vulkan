# Copyright (C) 2016-2024 by Sascha Willems - www.saschawillems.de
# This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
# Multithreaded optimization added for faster compilation

import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

parser = argparse.ArgumentParser(description='Compile all GLSL shaders')
parser.add_argument('--glslang', type=str, help='path to glslangvalidator executable')
parser.add_argument('--sample', type=str, help='can be used to compile shaders for a single sample only')
parser.add_argument('--g', action='store_true', help='compile with debug symbols')
args = parser.parse_args()

def findGlslang():
    def isExe(path):
        return os.path.isfile(path) and os.access(path, os.X_OK)

    if args.glslang != None and isExe(args.glslang):
        return args.glslang

    exe_name = "glslangValidator"
    if os.name == "nt":
        exe_name += ".exe"

    for exe_dir in os.environ["PATH"].split(os.pathsep):
        full_path = os.path.join(exe_dir, exe_name)
        if isExe(full_path):
            return full_path

    sys.exit("Could not find glslangvalidator executable on PATH, and was not specified with --glslang")

# Extracted compilation logic into a worker function for the threads
def compile_shader(glslang_path, input_file, output_file, add_params):
    # Added quotes around paths just in case your folder has spaces!
    cmd = f'"{glslang_path}" -V "{input_file}" -o "{output_file}" {add_params}'
    res = subprocess.call(cmd, shell=True)
    return res, input_file

file_extensions = tuple([".vert", ".frag", ".comp", ".geom", ".tesc", ".tese", ".rgen", ".rchit", ".rmiss", ".rahit", ".mesh", ".task"])
compile_single_sample = ""
if args.sample != None:
    compile_single_sample = args.sample
    if (not os.path.isdir(compile_single_sample)):
        print("ERROR: No directory found with name %s" % compile_single_sample)
        exit(-1)

glslang_path = findGlslang()
dir_path = os.path.dirname(os.path.realpath(__file__))
dir_path = dir_path.replace('\\', '/')

# 1. Gather all compilation tasks instead of running them immediately
tasks = []

for root, dirs, files in os.walk(dir_path):
    folder_name = os.path.basename(root)
    if (compile_single_sample != "" and folder_name != compile_single_sample):
        continue

    for file in files:
        if file.endswith(file_extensions):
            input_file = os.path.join(root, file)
            output_file = input_file + ".spv"

            add_params = ""
            if args.g:
                add_params = "-g"

            # FIX: Added .rahit to the target environment so it compiles properly!
            if file.endswith((".rgen", ".rchit", ".rmiss", ".rahit")):
               add_params += " --target-env vulkan1.2"
            # Same goes for samples that use ray queries
            if root.endswith("rayquery") and file.endswith(".frag"):
                add_params += " --target-env vulkan1.2"
            # Mesh and task shader also require different settings
            if file.endswith((".mesh", ".task")):
                add_params += " --target-env spirv1.4"

            # Queue the task
            tasks.append((glslang_path, input_file, output_file, add_params))

if not tasks:
    print("No shaders found to compile.")
    sys.exit(0)

# 2. Execute tasks concurrently
threads = os.cpu_count()
print(f"Compiling {len(tasks)} shaders using up to {threads} threads...")

with ThreadPoolExecutor(max_workers=threads) as executor:
    # Submit all jobs to the thread pool
    futures = [executor.submit(compile_shader, *task) for task in tasks]
    
    # As each thread finishes, check the result
    for future in as_completed(futures):
        res, completed_file = future.result()
        
        # If any shader fails to compile, stop everything and exit
        if res != 0:
            print(f"\n========================================")
            print(f"ERROR: Compilation failed for {completed_file}")
            print(f"========================================\n")
            # Attempt to cancel remaining tasks in the queue
            for f in futures:
                f.cancel()
            
            input("Press Enter to exit...") 
            sys.exit(res)

print("All shaders compiled successfully!")
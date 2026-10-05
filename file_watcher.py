import argparse
import copy
import json
import subprocess
import sys
import os
import time
import platform
import shutil

try:
    from watchdog.observers import Observer
    from watchdog.events import FileSystemEventHandler
except ImportError:
    print("'watchdog' not installed. Run 'pip install watchdog'.")
    sys.exit(1)


CURVE_JSON = "curve.json"
MASTER_SCENE_JSON = "scene_master.json"
SCENE_JSON = "scene.json"
GENERATE_PY = os.path.join(os.path.dirname(__file__), "generate_surface_mesh.py")


def find_exe(hint = None):
    candidates = [
        hint,
        "./app", "./app.exe",
        "./build/app", "./build/Debug/app.exe"
    ]
    
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    
    return None


class PipelineHandler(FileSystemEventHandler):
    def __init__(self, exe_path, obj_path, cooldown = 1.5):
        self.exe_path = exe_path
        self.obj_path = obj_path
        self.cooldown = cooldown
        self._last_run = 0
        self._watch_files = {
            os.path.abspath(CURVE_JSON),
            os.path.abspath(MASTER_SCENE_JSON)
        }
    
    def on_modified(self, event):
        if event.is_directory:
            return
        
        abs_path = os.path.abspath(event.src_path)
        
        if abs_path not in self._watch_files:
            return
        
        now = time.time()
        
        if now - self._last_run < self.cooldown:
            return
        
        self._last_run = now
        
        print(f"Change detected for file: {event.src_path}")
        self.run_pipeline()
    
    
    def generate_octant_scenes(self):
        with open(MASTER_SCENE_JSON) as f:
            master = json.load(f)
        
        grid = master["grid"]
        origin = grid["origin"]
        size = grid["size"]
        res = grid["resolution"]
        
        half_size = {axis: size[axis] / 2.0 for axis in "xyz"}
        half_res = {axis: res[axis] // 2 for axis in "xyz"}
        
        scenes = []
        
        for octant in range(8):
            x_bit = octant & 1
            y_bit = (octant >> 1) & 1
            z_bit = (octant >> 2) & 1
            
            octant_origin = {
                "x": origin["x"] + x_bit * half_size["x"],
                "y": origin["y"] + y_bit * half_size["y"],
                "z": origin["z"] + z_bit * half_size["z"]
            }
            
            scene = copy.deepcopy(master)
            scene["grid"]["origin"] = octant_origin
            scene["grid"]["size"] = half_size
            scene["grid"]["resolution"] = half_res
            
            scenes.append(scene)
        
        return scenes

    
    def run_single_scene(self):
        with open(MASTER_SCENE_JSON) as f:
            master = json.load(f)

        print("Rendering single scene (no octant splitting)...")

        with open(SCENE_JSON, "w") as f:
            json.dump(master, f, indent=4)

        result = subprocess.run([self.exe_path], capture_output=False)

        if result.returncode != 0:
            print(f"Renderer exited with code {result.returncode}.")

        print("Pipeline done. Waiting for changes...")
        
    
    
    def run_pipeline(self):
        if (os.path.exists(CURVE_JSON)):
            print(f"Step 1/2: Generating mesh...")
            
            result = subprocess.run(
                [sys.executable, GENERATE_PY, CURVE_JSON, self.obj_path],
                capture_output=False
            )
            
            if result.returncode != 0:
                print("ERROR: generate_mesh.py failed.")
                return
        else:
            print(f"{CURVE_JSON} not found, skipping mesh generation.")
        
        
        if not (self.exe_path and os.path.isfile(self.exe_path)):
            print("No renderer executable found, skipping renderer step.")
            return
        
        with open(MASTER_SCENE_JSON) as f:
            master = json.load(f)
            
        use_octant_splitting = master.get("use_octant_splitting", False)
        print(use_octant_splitting)
        
        if not use_octant_splitting:
            self.run_single_scene()
            return
        
        octant_scenes = self.generate_octant_scenes()
        
        for i, scene in enumerate(octant_scenes, start=1):
            print(f"Rendering octant {i}/8...")
            
            with open(SCENE_JSON, "w") as f:
                json.dump(scene, f, indent=4)
            
            result = subprocess.run([self.exe_path], capture_output=False)
            
            if result.returncode != 0:
                print(f"Renderer exited with code {result.returncode} for octant {i}.")
                continue
                
            for grid_name in ("primary", "bounce"):
                src = f"caustics_{grid_name}.vtu"
                dst = f"caustics_{grid_name}_octant_{i}.vtu"
                
                if os.path.exists(src):
                    shutil.move(src, dst)

        
        print("Pipeline done. Waiting for changes...") 


def main():
    parser = argparse.ArgumentParser(description="Auto-pipeline watcher")
    parser.add_argument("--exe", default="./app.exe", help="Path to compiled app.exe")
    parser.add_argument("--obj", default="Surface.obj", help="Output OBJ path")
    parser.add_argument("--watch-dir", default=".", help="Directory to watch")
    args = parser.parse_args()
    
    exe = find_exe(args.exe)
    
    if exe:
        print(f"Renderer: {exe}")
    else:
        print("No renderer found")
    
    handler = PipelineHandler(
        exe_path=exe,
        obj_path=args.obj
    )
    
    watch_dir = os.path.abspath(args.watch_dir)
    observer = Observer()
    observer.schedule(handler, watch_dir, recursive=False)
    observer.start()
    
    print(f"Watching: {watch_dir}")
    print(f"Files: {CURVE_JSON}, {SCENE_JSON}")
    
    handler.run_pipeline()
    
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\nfile_watcher stopped.")
        observer.stop()
    
    observer.join()


if __name__ == "__main__":
    main()
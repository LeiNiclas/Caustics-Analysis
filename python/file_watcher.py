import argparse
import copy
import hashlib
import json
import subprocess
import sys
import os
import tempfile
import time
import platform
import shutil
import threading


CONFIG_FILE_NAME = "workflow_config.json"
LOCAL_CONFIG_FILE_NAME = "workflow_config.local.json"
REFLECTION_FLAG = "compute_reflections"


def find_config_file(start_dir=None, config_name=CONFIG_FILE_NAME):
    current = os.path.abspath(start_dir or os.path.dirname(__file__))
    while True:
        candidate = os.path.join(current, config_name)
        if os.path.isfile(candidate):
            return candidate
        parent = os.path.dirname(current)
        if parent == current:
            return os.path.join(os.path.abspath(os.path.dirname(__file__)), config_name)
        current = parent


def load_json_file(path):
    if not path or not os.path.isfile(path):
        return {}
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def merge_dicts(base, override):
    result = copy.deepcopy(base)
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = merge_dicts(result[key], value)
        else:
            result[key] = value
    return result


def load_workflow_config():
    default_config_path = find_config_file(config_name=CONFIG_FILE_NAME)
    local_config_path = find_config_file(config_name=LOCAL_CONFIG_FILE_NAME)

    config = load_json_file(default_config_path)
    local_config = load_json_file(local_config_path) if local_config_path and os.path.isfile(local_config_path) else {}
    merged = merge_dicts(config, local_config)

    project_root = os.path.dirname(default_config_path)
    if local_config:
        print(f"Workflow config: using local override {local_config_path}")
    else:
        print(f"Workflow config: using default {default_config_path}")
    paths = merged.get("paths", {})
    temp_scene_dir = os.path.join(tempfile.gettempdir(), "caustics_analysis")
    os.makedirs(temp_scene_dir, exist_ok=True)
    temp_scene_path = os.path.join(temp_scene_dir, "scene.json")

    def resolve(path_value, default=None):
        if path_value is None:
            return default
        value = str(path_value).strip()
        if value == "":
            return default
        if os.path.isabs(value):
            return os.path.normpath(value)
        return os.path.normpath(os.path.join(project_root, value))

    return {
        "project_root": project_root,
        "working_directory": resolve(paths.get("working_directory", "."), os.path.join(project_root, "build", "Debug")),
        "curve_json": resolve(paths.get("curve_json", "curve.json"), os.path.join(project_root, "curve.json")),
        "master_scene_json": resolve(paths.get("master_scene_json", "scene_master.json"), os.path.join(project_root, "scene_master.json")),
        "scene_json": resolve(paths.get("scene_json", ""), temp_scene_path),
        "renderer_exe": resolve(paths.get("renderer_exe", "build/Debug/app.exe"), os.path.join(project_root, "build", "Debug", "app.exe")),
        "obj_output": resolve(paths.get("obj_output", "build/Debug/Surface.obj"), os.path.join(project_root, "build", "Debug", "Surface.obj")),
        "combined_obj": resolve(paths.get("combined_obj", "build/Debug/caustics_mesh_combined.obj"), os.path.join(project_root, "build", "Debug", "caustics_mesh_combined.obj")),
        "pvpython": resolve(paths.get("pvpython")),
        "plugin": resolve(paths.get("plugin")),
        "primary_vti": resolve(paths.get("primary_vti", "build/Debug/caustics_primary.vtu"), os.path.join(project_root, "build", "Debug", "caustics_primary.vtu")),
        "bounce_vti": resolve(paths.get("bounce_vti", "build/Debug/caustics_bounce.vtu"), os.path.join(project_root, "build", "Debug", "caustics_bounce.vtu")),
        "reflection_vtp": resolve(paths.get("reflection_vtp", "build/Debug/caustic_reflection_points.vtp"), os.path.join(project_root, "build", "Debug", "caustic_reflection_points.vtp")),
    }


WORKFLOW_CONFIG = load_workflow_config()
PROJECT_ROOT = WORKFLOW_CONFIG["project_root"]
PYTHON_DIR = os.path.join(PROJECT_ROOT, "python")
EXTRACT_PY = os.path.join(PYTHON_DIR, "extract_caustic_meshes.py")
GENERATE_PY = os.path.join(PYTHON_DIR, "generate_surface_mesh.py")
CURVE_JSON = WORKFLOW_CONFIG["curve_json"]
MASTER_SCENE_JSON = WORKFLOW_CONFIG["master_scene_json"]
SCENE_JSON = WORKFLOW_CONFIG["scene_json"]
COMBINED_OBJ = WORKFLOW_CONFIG["combined_obj"]


try:
    from watchdog.observers import Observer
    from watchdog.events import FileSystemEventHandler
except ImportError:
    print("'watchdog' not installed. Run 'pip install watchdog'.")
    sys.exit(1)


def file_hash(path):
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def find_exe(hint = None):
    candidates = [
        hint,
        WORKFLOW_CONFIG["renderer_exe"],
        os.path.join(WORKFLOW_CONFIG["working_directory"], "app.exe"),
        os.path.join(WORKFLOW_CONFIG["working_directory"], "app"),
        os.path.join(WORKFLOW_CONFIG["project_root"], "build", "Debug", "app.exe"),
        os.path.join(WORKFLOW_CONFIG["project_root"], "app.exe")
    ]
    
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    
    return None


def resolve_executable(path_value):
    if not path_value:
        return None
    value = str(path_value).strip()
    if not value:
        return None
    if os.path.isfile(value):
        return value
    resolved = shutil.which(value)
    if resolved:
        return resolved
    return None


def normalize_obj_path(value):
    if not value:
        return None
    raw = str(value).strip()
    if not raw:
        return None
    if os.path.isabs(raw):
        return os.path.normcase(os.path.normpath(raw))
    return os.path.normcase(os.path.normpath(os.path.abspath(os.path.join(WORKFLOW_CONFIG["working_directory"], raw))))


class PipelineHandler(FileSystemEventHandler):
    def __init__(self, exe_path, obj_path, pvpython_path=None, plugin_path="RidgeSurface", cooldown=1.5, work_dir=None):
        self.exe_path = exe_path
        self.obj_path = obj_path
        self.pvpython_path = pvpython_path
        self.plugin_path = plugin_path
        self.work_dir = work_dir or WORKFLOW_CONFIG["working_directory"]
        self.cooldown = cooldown
        self._last_run = 0
        self._watch_files = {
            os.path.abspath(CURVE_JSON),
            os.path.abspath(MASTER_SCENE_JSON)
        }
        
        self._ignore_hash = None
        self._lock = threading.Lock()
        self._pending = False
        
    
    def on_modified(self, event):
        if event.is_directory:
            return
        
        abs_path = os.path.abspath(event.src_path)
        
        if abs_path not in self._watch_files:
            return
        
        if abs_path == os.path.abspath(MASTER_SCENE_JSON):
            try:
                if file_hash(abs_path) == self._ignore_hash:
                    return
            except OSError:
                return
        
        now = time.time()
        
        if now - self._last_run < self.cooldown:
            return
        
        self._last_run = now
        
        print(f"Change detected for file: {event.src_path}")
        self.trigger()
    
    
    def trigger(self):
        if not self._lock.acquire(blocking=False):
            print("Pipeline läuft bereits, Änderung ignoriert.")
            return
        try:
            while True:
                self._pending = False
                self.run_pipeline()
                if not self._pending:
                    break
        except Exception as e:
            import traceback
            traceback.print_exc()
        finally:
            self._last_run = time.time()
            self._lock.release()
    
    
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

        result = subprocess.run([self.exe_path, SCENE_JSON], cwd=self.work_dir, capture_output=False)

        if result.returncode != 0:
            print(f"Renderer exited with code {result.returncode}.")

        print("Pipeline done. Waiting for changes...")
        
    
    def generate_implicit_preview(self, master):
        implicit_surface = master.get("implicitSurface", {})
        preview = implicit_surface.get("preview", {})
        if not preview.get("enabled", True):
            return True

        print("Generating implicit surface preview...")
        with open(SCENE_JSON, "w") as f:
            json.dump(master, f, indent=4)

        result = subprocess.run(
            [self.exe_path, SCENE_JSON, "--preview-only"],
            cwd=self.work_dir,
            capture_output=False
        )
        if result.returncode != 0:
            print(f"Implicit surface preview generation failed with code {result.returncode}.")
            return False
        return True

    
    def run_pipeline(self):
        with open(MASTER_SCENE_JSON) as f:
            master = json.load(f)
        
        if master.get(REFLECTION_FLAG, False):
            if self.exe_path and os.path.isfile(self.exe_path):
                if not self.generate_implicit_preview(master):
                    return
                self.run_reflection_stage(master)
            return
        
        if (os.path.exists(CURVE_JSON)):
            print(f"Step 1/2: Generating mesh...")
            
            result = subprocess.run(
                [sys.executable, GENERATE_PY, CURVE_JSON, self.obj_path],
                cwd=self.work_dir,
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

        if not self.generate_implicit_preview(master):
            return

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
            
            result = subprocess.run([self.exe_path, SCENE_JSON], cwd=self.work_dir, capture_output=False)
            
            if result.returncode != 0:
                print(f"Renderer exited with code {result.returncode} for octant {i}.")
                continue
                
            for grid_name in ("primary", "bounce"):
                src = os.path.join(self.work_dir, f"caustics_{grid_name}.vtu")
                dst = os.path.join(self.work_dir, f"caustics_{grid_name}_octant_{i}.vtu")
                
                if os.path.exists(src):
                    os.replace(src, dst)

        self.extract_and_retrigger()
        
        print("Pipeline done. Waiting for changes...")
    
    
    def write_master(self, master, ignore=False):
        with open(MASTER_SCENE_JSON, "w") as f:
            json.dump(master, f, indent=4)
        if ignore:
            self._ignore_hash = file_hash(MASTER_SCENE_JSON)

    def run_reflection_stage(self, master):
        print("Stage 2: Rendering with reflection points...")
        self.run_single_scene()
        master[REFLECTION_FLAG] = False
        for m in master["meshes"]:
            if normalize_obj_path(m.get("objPath")) == normalize_obj_path(COMBINED_OBJ):
                m["isCausticsMesh"] = False
        self.write_master(master, ignore=True)

    def extract_and_retrigger(self):
        pvpython_path = resolve_executable(self.pvpython_path)
        if not pvpython_path:
            print("No valid pvpython found. Skipping mesh extraction. Set 'pvpython' in workflow_config.local.json to enable ParaView export.")
            return

        if not self.plugin_path:
            print("No plugin path configured. Skipping mesh extraction.")
            return

        if not os.path.exists(self.plugin_path):
            print(f"Plugin not found: {self.plugin_path}. Skipping mesh extraction.")
            return

        with open(MASTER_SCENE_JSON) as f:
            master = json.load(f)

        print("Extracting + combining caustic meshes...")
        result = subprocess.run([
            pvpython_path, EXTRACT_PY,
            "--factor", str(master.get("caustic_threshold_factor", 0.25)),
            "--plugin", self.plugin_path,
            "--out", COMBINED_OBJ
        ], cwd=self.work_dir)
        if result.returncode != 0:
            print("ERROR: mesh extraction failed.")
            return

        for m in master["meshes"]:
            if normalize_obj_path(m.get("objPath")) == normalize_obj_path(COMBINED_OBJ):
                m["isCausticsMesh"] = True

        master[REFLECTION_FLAG] = True
        self.write_master(master, ignore=True)
        self._pending = True


def main():
    parser = argparse.ArgumentParser(description="Auto-pipeline watcher")
    parser.add_argument("--exe", default=WORKFLOW_CONFIG["renderer_exe"], help="Path to compiled app.exe")
    parser.add_argument("--obj", default=WORKFLOW_CONFIG["obj_output"], help="Output OBJ path")
    parser.add_argument("--watch-dir", default=WORKFLOW_CONFIG["working_directory"], help="Directory to watch")
    parser.add_argument("--pvpython", default=WORKFLOW_CONFIG["pvpython"], help="Path to pvpython.exe")
    parser.add_argument("--plugin", default=WORKFLOW_CONFIG["plugin"], help="RidgeSurface plugin name or path")
    args = parser.parse_args()
    
    exe = find_exe(args.exe)
    
    if exe:
        print(f"Renderer: {exe}")
    else:
        print("No renderer found")
    
    handler = PipelineHandler(
        exe_path=exe,
        obj_path=args.obj,
        pvpython_path=args.pvpython,
        plugin_path=args.plugin,
        work_dir=WORKFLOW_CONFIG["working_directory"]
    )
    
    watch_dir = os.path.abspath(args.watch_dir)
    observer = Observer()
    observer.schedule(handler, watch_dir, recursive=False)
    observer.start()
    
    print(f"Watching: {watch_dir}")
    print(f"Files: {CURVE_JSON}, {SCENE_JSON}")
    
    handler.trigger()
    
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\nfile_watcher stopped.")
        observer.stop()
    
    observer.join()


if __name__ == "__main__":
    main()
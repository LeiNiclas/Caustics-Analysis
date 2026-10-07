import json
import sys
import os


def catmull_rom(p0, p1, p2, p3, t, alpha = 0.5):
    t2 = t * t
    t3 = t2 * t
    a = alpha
    
    return (
        (-a * t3 + 2 * a * t2 - a * t)              * p0 +
        ((2 - a) * t3 + (a - 3) * t2 + 1)           * p1 +
        ((a - 2) * t3 + (3 - 2 * a) * t2 + a * t)   * p2 +
        (a * t3 - a * t2)                           * p3
    )


def eval_spline(control_points, resolution, tension):
    n = len(control_points)
    xs = [p[0] for p in control_points]
    zs = [p[1] for p in control_points]
    
    result = []
    steps_per_segment = max(2, resolution // (n - 1))
    
    for segment in range(n - 1):
        i0 = max(0, segment - 1)
        i1 = segment
        i2 = segment + 1
        i3 = min(n - 1, segment + 2)
        
        for k in range(steps_per_segment):
            t = k / steps_per_segment
            z = catmull_rom(zs[i0], zs[i1], zs[i2], zs[i3], t, tension)
            x = xs[i1] + t * (xs[i2] - xs[i1])
            result.append((x, z))
    
    result.append((xs[-1], zs[-1]))
    return result


def generate_obj(curve_path = "curve.json", obj_path = "Surface.obj"):
    if not os.path.exists(curve_path):
        print(f"ERROR: {curve_path} not found.")
        sys.exit(1)
    
    with open(curve_path) as f:
        data = json.load(f)
    
    control_points = [(cp["x"], cp["z"]) for cp in data["controlPoints"]]
    resolution = data.get("resolution", 50)
    tension = data.get("tension", 0.5)
    y_start = data.get("yStart", -0.5)
    y_end = data.get("yEnd", 0.5)
    
    curve = eval_spline(control_points=control_points, resolution=1000, tension=tension)
    N = len(curve)
    
    # Row 0: y = y_start (front edge)
    # Row 1: y = y_end (back edge, copy of curve)
    verts = []
    
    for (x, z) in curve:
        verts.append((x, y_start, z))
    for (x, z) in curve:
        verts.append((x, y_end, z))
    
    faces = []
    
    for i in range(N - 1):
        v00 = i + 1
        v10 = i + 2
        v01 = i + N + 1
        v11 = i + N + 2
        
        faces.append((v00, v10, v11))
        faces.append((v00, v11, v01))
    
    with open(obj_path, "w") as f:
        f.write("o Surface\n")
        
        for (x, y, z) in verts:
            f.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
        for (a, b, c) in faces:
            f.write(f"f {a} {b} {c}\n")
    
    print(f"{N} curve points | {len(verts)} vertices | {len(faces)} faces -> {obj_path}")


if __name__ == "__main__":
    curve_path = sys.argv[1] if len(sys.argv) > 1 else "curve.json"
    obj_path = sys.argv[2] if len(sys.argv) > 2 else "Surface.obj"
    
    generate_obj(curve_path=curve_path, obj_path=obj_path)
import argparse, glob, os
import numpy as np
from paraview.simple import *
from paraview import servermanager as sm
from vtkmodules.util.numpy_support import vtk_to_numpy

ap = argparse.ArgumentParser()
ap.add_argument("--factor", type=float, default=0.25)
ap.add_argument("--smoothing", type=int, default=2)
ap.add_argument("--field", default="bounce")
ap.add_argument("--dir", default=".")
ap.add_argument("--plugin", default="RidgeSurface")
ap.add_argument("--out", default="caustics_mesh_combined.obj")
args = ap.parse_args()

LoadPlugin(args.plugin, remote=False, ns=globals())

files = sorted(glob.glob(os.path.join(args.dir, f"caustics_{args.field}_octant_*.vtu")))
if not files:
    raise SystemExit("Keine Octant-VTUs gefunden.")

point_sources = []
for f in files:
    p = CellDatatoPointData(Input=XMLUnstructuredGridReader(FileName=[f]))
    p.UpdatePipeline()
    point_sources.append(p)

global_max = max(p.PointData[args.field].GetRange()[1] for p in point_sources)
threshold = args.factor * global_max
print(f"Globales Maximum: {global_max:.4f} -> Schwellwert: {threshold:.4f}")

ridges = []
for p in point_sources:
    r = VCGRidgeSurface(Input=p)
    r.Set(SmoothingRange=args.smoothing, ScalarMin=threshold, ClipScalarMin=threshold)
    r.UpdatePipeline()
    ridges.append(r)

# Zusammenfügen -> Oberfläche -> Dreiecke
combined = AppendDatasets(Input=ridges)
tri = Triangulate(Input=ExtractSurface(Input=combined))
tri.UpdatePipeline()

data = sm.Fetch(tri)
pts = vtk_to_numpy(data.GetPoints().GetData())
faces = vtk_to_numpy(data.GetPolys().GetConnectivityArray()).reshape(-1, 3)

with open(args.out, "w") as f:
    np.savetxt(f, pts, fmt="v %.6f %.6f %.6f")
    np.savetxt(f, faces + 1, fmt="f %d %d %d")

print(f"-> {args.out}: {len(pts)} Verts, {len(faces)} Tris")
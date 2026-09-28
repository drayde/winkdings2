// MYOUNG BS-02-A1AJ010 / LCSC C5239862 CR2032 holder.
// Simplified model from the manufacturer drawing: 24.5 x 22.1 x 8.5 mm.
SetFactory("OpenCASCADE");

Cylinder(1) = {0, 0, 0, 0, 0, 8.5, 11.05, 2*Pi};
Cylinder(2) = {0, 0, 1.0, 0, 0, 7.6, 10.05, 2*Pi};
BooleanDifference{ Volume{1}; Delete; }{ Volume{2}; Delete; }

// The connector-side extension gives the specified 24.5 mm overall length.
Box(10) = {10.0, -3.75, 0, 2.25, 7.5, 8.5};

Save "MYOUNG_BS-02-A1AJ010.step";

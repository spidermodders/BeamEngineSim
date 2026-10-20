#ifndef ATG_ENGINE_SIM_UTILITIES_H
#define ATG_ENGINE_SIM_UTILITIES_H

double modularDistance(double a, double b, double mod = 1.0);
double positiveMod(double x, double mod);
double erfApproximation(double x);

// Angles in radians, [0, 2pi): direction angle, CCW distance a to b, and CCW arc overlap length
double vectorAngle(double x, double y);
double positiveAngularDistance(double a, double b);
double angularOverlap(double a0, double a1, double b0, double b1);

template <typename t>
inline t clamp(t x, t x0 = static_cast<t>(0.0), t x1 = static_cast<t>(1.0)) {
    if (x <= x0) return x0;
    else if (x >= x1) return x1;
    else return x;
}

#endif /* ATG_ENGINE_SIM_UTILITIES_H */

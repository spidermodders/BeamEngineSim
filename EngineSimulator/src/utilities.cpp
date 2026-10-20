#include "../include/utilities.h"

#include <cmath>

double modularDistance(double a0, double b0, double mod) {
    double a, b;
    if (a0 < b0) {
        a = a0;
        b = b0;
    }
    else {
        a = b0;
        b = a0;
    }

    return std::fmin(b - a, a + mod - b);
}

double positiveMod(double x, double mod) {
    if (x < 0) {
        x = std::ceil(-x / mod) * mod + x;
    }

    return std::fmod(x, mod);
}

double vectorAngle(double x, double y) {
    const double angle = std::atan2(y, x);
    return angle < 0 ? angle + 2 * 3.14159265358979323846 : angle;
}

double positiveAngularDistance(double a, double b) {
    if (b <= a) b += 2 * 3.14159265358979323846;
    return b - a;
}

double angularOverlap(double a0, double a1, double b0, double b1) {
    constexpr double twoPi = 2 * 3.14159265358979323846;
    double a[2][2], b[2][2];
    auto split = [&](double s, double e, double out[2][2]) {
        s = positiveMod(s, twoPi);
        e = positiveMod(e, twoPi);
        if (s <= e) { out[0][0] = s; out[0][1] = e; out[1][0] = out[1][1] = 0; }
        else { out[0][0] = s; out[0][1] = twoPi; out[1][0] = 0; out[1][1] = e; }
    };
    split(a0, a1, a);
    split(b0, b1, b);

    double overlap = 0;
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            overlap += std::fmax(0.0, std::fmin(a[i][1], b[j][1]) - std::fmax(a[i][0], b[j][0]));
        }
    }

    return overlap;
}

double erfApproximation(double x) {
    const double a1 = 0.278393;
    const double a2 = 0.230389;
    const double a3 = 0.000972;
    const double a4 = 0.078108;

    const double x2 = x * x;
    const double x3 = x2 * x;
    const double x4 = x3 * x;

    const double q = 1 / (1 + a1 * x + a2 * x2 + a3 * x3 + a4 * x4);
    const double q2 = q * q;
    const double q4 = q2 * q2;

    return 1 - q4;
}

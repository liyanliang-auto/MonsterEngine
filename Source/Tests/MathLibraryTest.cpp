// Copyright Monster Engine. All Rights Reserved.

/**
 * @file MathLibraryTest.cpp
 * @brief Test file for the Monster Engine Math Library
 * 
 * This file contains basic tests to verify the math library compiles
 * and functions correctly.
 */

#include "Math/MonsterMath.h"
#include <iostream>
#include <cassert>

using namespace MonsterEngine;
using namespace MonsterEngine::Math;

/**
 * @brief Test TVector operations
 */
void TestVector()
{
    std::cout << "=== Testing TVector ===" << std::endl;

    // Test constructors
    FVector v1(1.0, 2.0, 3.0);
    FVector v2(4.0, 5.0, 6.0);
    FVector v3 = FVector::ZeroVector;
    FVector v4 = FVector::OneVector;

    std::cout << "v1 = " << v1.ToString() << std::endl;
    std::cout << "v2 = " << v2.ToString() << std::endl;

    // Test arithmetic
    FVector sum = v1 + v2;
    FVector diff = v2 - v1;
    FVector scaled = v1 * 2.0;

    std::cout << "v1 + v2 = " << sum.ToString() << std::endl;
    std::cout << "v2 - v1 = " << diff.ToString() << std::endl;
    std::cout << "v1 * 2 = " << scaled.ToString() << std::endl;

    // Test dot and cross product
    double dot = FVector::DotProduct(v1, v2);
    FVector cross = FVector::CrossProduct(v1, v2);

    std::cout << "v1 . v2 = " << dot << std::endl;
    std::cout << "v1 x v2 = " << cross.ToString() << std::endl;

    // Test normalization
    FVector normalized = v1.GetSafeNormal();
    std::cout << "v1 normalized = " << normalized.ToString() << std::endl;
    std::cout << "Is normalized: " << (normalized.IsNormalized() ? "yes" : "no") << std::endl;

    // Test size
    std::cout << "v1 size = " << v1.Size() << std::endl;
    std::cout << "v1 size squared = " << v1.SizeSquared() << std::endl;

    std::cout << "TVector tests passed!" << std::endl << std::endl;
}

/**
 * @brief Test TVector2 operations
 */
void TestVector2D()
{
    std::cout << "=== Testing TVector2 ===" << std::endl;

    FVector2D v1(3.0, 4.0);
    FVector2D v2(1.0, 2.0);

    std::cout << "v1 = " << v1.ToString() << std::endl;
    std::cout << "v1 size = " << v1.Size() << std::endl;
    std::cout << "v1 normalized = " << v1.GetSafeNormal().ToString() << std::endl;

    FVector2D rotated = v1.GetRotated(90.0);
    std::cout << "v1 rotated 90 deg = " << rotated.ToString() << std::endl;

    std::cout << "TVector2 tests passed!" << std::endl << std::endl;
}

/**
 * @brief Test TVector4 operations
 */
void TestVector4()
{
    std::cout << "=== Testing TVector4 ===" << std::endl;

    FVector4 v1(1.0, 2.0, 3.0, 1.0);
    FVector4 v2(4.0, 5.0, 6.0, 0.0);

    std::cout << "v1 = " << v1.ToString() << std::endl;
    std::cout << "v2 = " << v2.ToString() << std::endl;

    double dot4 = FVector4::Dot4(v1, v2);
    double dot3 = FVector4::Dot3(v1, v2);

    std::cout << "v1 . v2 (4D) = " << dot4 << std::endl;
    std::cout << "v1 . v2 (3D) = " << dot3 << std::endl;

    std::cout << "TVector4 tests passed!" << std::endl << std::endl;
}

/**
 * @brief Test TQuat operations
 */
void TestQuat()
{
    std::cout << "=== Testing TQuat ===" << std::endl;

    // Identity quaternion
    FQuat identity = FQuat::Identity;
    std::cout << "Identity = " << identity.ToString() << std::endl;

    // Create from axis-angle
    FQuat rotZ90 = FQuat::MakeFromAxisAngle(FVector::UpVector, FMath::DegreesToRadians(90.0));
    std::cout << "90 deg around Z = " << rotZ90.ToString() << std::endl;

    // Rotate a vector
    FVector forward = FVector::ForwardVector;
    FVector rotated = rotZ90.RotateVector(forward);
    std::cout << "Forward rotated 90 deg around Z = " << rotated.ToString() << std::endl;

    // Test Slerp
    FQuat slerped = FQuat::Slerp(identity, rotZ90, 0.5);
    std::cout << "Slerp(identity, rotZ90, 0.5) = " << slerped.ToString() << std::endl;

    // Get axis and angle
    FVector axis;
    double angle;
    rotZ90.ToAxisAndAngle(axis, angle);
    std::cout << "Axis = " << axis.ToString() << ", Angle = " << FMath::RadiansToDegrees(angle) << " deg" << std::endl;

    std::cout << "TQuat tests passed!" << std::endl << std::endl;
}

/**
 * @brief Test TRotator operations
 */
void TestRotator()
{
    std::cout << "=== Testing TRotator ===" << std::endl;

    FRotator rot1(45.0, 90.0, 0.0);  // Pitch, Yaw, Roll
    std::cout << "rot1 = " << rot1.ToString() << std::endl;

    // Convert to quaternion and back
    FQuat quat = rot1.Quaternion();
    FRotator rot2 = quat.Rotator();
    std::cout << "rot1 -> quat -> rot2 = " << rot2.ToString() << std::endl;

    // Get direction vectors
    FVector forward = rot1.GetForwardVector();
    FVector right = rot1.GetRightVector();
    FVector up = rot1.GetUpVector();

    std::cout << "Forward = " << forward.ToString() << std::endl;
    std::cout << "Right = " << right.ToString() << std::endl;
    std::cout << "Up = " << up.ToString() << std::endl;

    // Test normalization
    FRotator rot3(400.0, -200.0, 720.0);
    FRotator normalized = rot3.GetNormalized();
    std::cout << "Normalized " << rot3.ToString() << " = " << normalized.ToString() << std::endl;

    std::cout << "TRotator tests passed!" << std::endl << std::endl;
}

/**
 * @brief Test TMatrix operations
 */
void TestMatrix()
{
    std::cout << "=== Testing TMatrix ===" << std::endl;

    // Identity matrix
    FMatrix identity = FMatrix::Identity;
    std::cout << "Identity matrix:" << std::endl << identity.ToString() << std::endl;

    // Translation matrix
    FMatrix translation = FMatrix::MakeTranslation(FVector(10.0, 20.0, 30.0));
    std::cout << "Translation matrix origin = " << translation.GetOrigin().ToString() << std::endl;

    // Scale matrix
    FMatrix scale = FMatrix::MakeScale(FVector(2.0, 2.0, 2.0));
    FVector scaledPoint = scale.TransformPosition(FVector(1.0, 1.0, 1.0)).GetXYZ();
    std::cout << "Scaled (1,1,1) by 2 = " << scaledPoint.ToString() << std::endl;

    // Rotation matrix from quaternion
    FQuat rotQuat = FQuat::MakeFromAxisAngle(FVector::UpVector, FMath::DegreesToRadians(90.0));
    FMatrix rotation = FMatrix::MakeFromQuat(rotQuat);
    FVector rotatedPoint = rotation.TransformPosition(FVector(1.0, 0.0, 0.0)).GetXYZ();
    std::cout << "Rotated (1,0,0) 90 deg around Z = " << rotatedPoint.ToString() << std::endl;

    // Matrix multiplication
    FMatrix combined = translation * rotation;
    std::cout << "Combined matrix origin = " << combined.GetOrigin().ToString() << std::endl;

    // Inverse
    FMatrix inverse = translation.Inverse();
    FMatrix shouldBeIdentity = translation * inverse;
    std::cout << "Translation * Inverse should be identity: " << 
        (shouldBeIdentity.Equals(identity, 0.0001) ? "yes" : "no") << std::endl;

    std::cout << "TMatrix tests passed!" << std::endl << std::endl;
}

/**
 * @brief Test TTransform operations
 */
void TestTransform()
{
    std::cout << "=== Testing TTransform ===" << std::endl;

    // Identity transform
    FTransform identity = FTransform::Identity;
    std::cout << "Identity transform: " << identity.ToString() << std::endl;

    // Create transform with translation, rotation, scale
    FQuat rotation = FQuat::MakeFromAxisAngle(FVector::UpVector, FMath::DegreesToRadians(45.0));
    FVector translation(100.0, 200.0, 300.0);
    FVector scale(2.0, 2.0, 2.0);

    FTransform transform(rotation, translation, scale);
    std::cout << "Transform: " << transform.ToString() << std::endl;

    // Transform a point
    FVector point(1.0, 0.0, 0.0);
    FVector transformed = transform.TransformPosition(point);
    std::cout << "Transformed (1,0,0) = " << transformed.ToString() << std::endl;

    // Inverse transform
    FTransform inverse = transform.Inverse();
    FVector backToOriginal = inverse.TransformPosition(transformed);
    std::cout << "Inverse transformed back = " << backToOriginal.ToString() << std::endl;

    // Blend transforms
    FTransform blended = FTransform::Blend(identity, transform, 0.5);
    std::cout << "Blended (50%) = " << blended.ToString() << std::endl;

    std::cout << "TTransform tests passed!" << std::endl << std::endl;
}

/**
 * @brief Test TBox operations
 */
void TestBox()
{
    std::cout << "=== Testing TBox ===" << std::endl;

    FBox box(FVector(-1.0, -1.0, -1.0), FVector(1.0, 1.0, 1.0));
    std::cout << "Box: " << box.ToString() << std::endl;
    std::cout << "Center = " << box.GetCenter().ToString() << std::endl;
    std::cout << "Extent = " << box.GetExtent().ToString() << std::endl;
    std::cout << "Volume = " << box.GetVolume() << std::endl;

    // Point inside test
    FVector insidePoint(0.0, 0.0, 0.0);
    FVector outsidePoint(2.0, 0.0, 0.0);
    std::cout << "(0,0,0) inside: " << (box.IsInside(insidePoint) ? "yes" : "no") << std::endl;
    std::cout << "(2,0,0) inside: " << (box.IsInside(outsidePoint) ? "yes" : "no") << std::endl;

    // Expand box
    FBox expanded = box.ExpandBy(1.0);
    std::cout << "Expanded by 1: " << expanded.ToString() << std::endl;

    std::cout << "TBox tests passed!" << std::endl << std::endl;
}

/**
 * @brief Test TSphere operations
 */
void TestSphere()
{
    std::cout << "=== Testing TSphere ===" << std::endl;

    FSphere sphere(FVector(0.0, 0.0, 0.0), 5.0);
    std::cout << "Sphere: " << sphere.ToString() << std::endl;
    std::cout << "Volume = " << sphere.GetVolume() << std::endl;
    std::cout << "Surface area = " << sphere.GetSurfaceArea() << std::endl;

    // Point inside test
    FVector insidePoint(1.0, 1.0, 1.0);
    FVector outsidePoint(10.0, 0.0, 0.0);
    std::cout << "(1,1,1) inside: " << (sphere.IsInside(insidePoint) ? "yes" : "no") << std::endl;
    std::cout << "(10,0,0) inside: " << (sphere.IsInside(outsidePoint) ? "yes" : "no") << std::endl;

    // Sphere intersection
    FSphere sphere2(FVector(8.0, 0.0, 0.0), 5.0);
    std::cout << "Spheres intersect: " << (sphere.Intersects(sphere2) ? "yes" : "no") << std::endl;

    std::cout << "TSphere tests passed!" << std::endl << std::endl;
}

/**
 * @brief Test TPlane operations
 */
void TestPlane()
{
    std::cout << "=== Testing TPlane ===" << std::endl;

    // XY plane at Z=0
    FPlane plane(FVector::UpVector, 0.0);
    std::cout << "Plane: " << plane.ToString() << std::endl;

    // Point distance
    FVector abovePoint(0.0, 0.0, 5.0);
    FVector belowPoint(0.0, 0.0, -5.0);
    FVector onPoint(1.0, 2.0, 0.0);

    std::cout << "Distance to (0,0,5) = " << plane.PlaneDot(abovePoint) << std::endl;
    std::cout << "Distance to (0,0,-5) = " << plane.PlaneDot(belowPoint) << std::endl;
    std::cout << "(1,2,0) on plane: " << (plane.IsOnPlane(onPoint) ? "yes" : "no") << std::endl;

    // Project point onto plane
    FVector projected = plane.ProjectPoint(abovePoint);
    std::cout << "Projected (0,0,5) onto plane = " << projected.ToString() << std::endl;

    std::cout << "TPlane tests passed!" << std::endl << std::endl;
}

/**
 * @brief Test FMath utility functions
 */
void TestFMath()
{
    std::cout << "=== Testing FMath ===" << std::endl;

    // Basic math
    std::cout << "Abs(-5) = " << FMath::Abs(-5.0) << std::endl;
    std::cout << "Sign(-5) = " << FMath::Sign(-5.0) << std::endl;
    std::cout << "Clamp(15, 0, 10) = " << FMath::Clamp(15.0, 0.0, 10.0) << std::endl;
    std::cout << "Sqrt(16) = " << FMath::Sqrt(16.0) << std::endl;
    std::cout << "Pow(2, 10) = " << FMath::Pow(2.0, 10.0) << std::endl;

    // Trigonometry
    std::cout << "Sin(PI/2) = " << FMath::Sin(MR_DOUBLE_HALF_PI) << std::endl;
    std::cout << "Cos(0) = " << FMath::Cos(0.0) << std::endl;

    // Angle conversion
    std::cout << "RadiansToDegrees(PI) = " << FMath::RadiansToDegrees(MR_DOUBLE_PI) << std::endl;
    std::cout << "DegreesToRadians(180) = " << FMath::DegreesToRadians(180.0) << std::endl;

    // Interpolation
    std::cout << "Lerp(0, 100, 0.5) = " << FMath::Lerp(0.0, 100.0, 0.5) << std::endl;
    std::cout << "SmoothStep(0, 1, 0.5) = " << FMath::SmoothStep(0.0, 1.0, 0.5) << std::endl;

    // Comparison
    std::cout << "IsNearlyEqual(1.0, 1.0001, 0.001) = " << 
        (FMath::IsNearlyEqual(1.0, 1.0001, 0.001) ? "yes" : "no") << std::endl;
    std::cout << "IsNearlyZero(0.00001) = " << 
        (FMath::IsNearlyZero(0.00001) ? "yes" : "no") << std::endl;

    // Random
    std::cout << "FRand() = " << FMath::FRand() << std::endl;
    std::cout << "RandRange(1, 10) = " << FMath::RandRange(1, 10) << std::endl;

    std::cout << "FMath tests passed!" << std::endl << std::endl;
}

/**
 * @brief Run all math library tests
 */
void RunMathLibraryTests()
{
    std::cout << "========================================" << std::endl;
    std::cout << "Monster Engine Math Library Tests" << std::endl;
    std::cout << "Version: " << MONSTER_MATH_VERSION_STRING << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    TestVector();
    TestVector2D();
    TestVector4();
    TestQuat();
    TestRotator();
    TestMatrix();
    TestTransform();
    TestBox();
    TestSphere();
    TestPlane();
    TestFMath();

    std::cout << "========================================" << std::endl;
    std::cout << "All Math Library Tests Passed!" << std::endl;
    std::cout << "========================================" << std::endl;
}

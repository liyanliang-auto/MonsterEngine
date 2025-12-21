#pragma once

/**
 * Render Dependency Graph (RDG) System
 * 
 * Main include file for the RDG system.
 * Reference: UE5 RenderGraph.h
 * 
 * Usage:
 * 1. Create FRDGBuilder with RHI device
 * 2. Create resources with CreateTexture/CreateBuffer
 * 3. Add passes with AddPass, declaring dependencies
 * 4. Call Execute to compile and run the graph
 * 
 * Example:
 * 
 *   FRDGBuilder graphBuilder(rhiDevice, "MyGraph");
 *   
 *   // Create shadow map
 *   FRDGTextureRef shadowMap = graphBuilder.CreateTexture(
 *       "ShadowMap",
 *       FRDGTextureDesc::CreateDepth(1024, 1024));
 *   
 *   // Shadow depth pass
 *   graphBuilder.AddPass("ShadowDepth", ERDGPassFlags::Raster,
 *       [&](FRDGPassBuilder& builder) {
 *           builder.WriteDepth(shadowMap);
 *       },
 *       [=](IRHICommandList& cmdList) {
 *           // Render shadow depth
 *       });
 *   
 *   // Shadow projection pass
 *   graphBuilder.AddPass("ShadowProjection", ERDGPassFlags::Raster,
 *       [&](FRDGPassBuilder& builder) {
 *           builder.ReadTexture(shadowMap);
 *       },
 *       [=](IRHICommandList& cmdList) {
 *           // Project shadows
 *       });
 *   
 *   // Execute graph (automatic transition insertion)
 *   graphBuilder.Execute(rhiCmdList);
 */

#include "RDG/RDGFwd.h"
#include "RDG/RDGDefinitions.h"
#include "RDG/RDGResource.h"
#include "RDG/RDGPass.h"
#include "RDG/RDGBuilder.h"

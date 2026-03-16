# Shelf toggles + listen audit — 2026-03-16

## Problem
1. Polish shelf icons (low/high) are purely decorative — no click, no DSP effect.
2. Verify listen toggle is visible for all 4 products (denoiser, deverb, polish, proximity).

## Listen audit result
All 4 products set `listenParamId = "listen"` → framework shows button. ✓

## Plan — shelf toggles

### 1. `ProductIdentity` — add two optional param IDs
- `lowShelfParamId`  (empty = not used)
- `highShelfParamId` (empty = not used)

### 2. `createSimpleParameterLayout` — add bool params with default=true when set

### 3. `VxPolishProcessor.cpp`
- Set `identity.lowShelfParamId = "demud_on"`, `highShelfParamId = "deess_on"`
- In `processProduct()`: read flags; if `demud_on` false → `params.deMud = 0`; if `deess_on` false → `params.deEss = 0` (and zero `params.troubleSmooth` for high-shelf)

### 4. `EditorBase`
- In `paint()`: dim shelf icon (alpha 0.25) when its param is false
- Override `mouseDown()`: if click hits `lowShelfIconBounds` or `highShelfIconBounds`, toggle the parameter

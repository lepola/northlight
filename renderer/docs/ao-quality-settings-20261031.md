# Optional AO quality selection

AOQuality selects High, Medium or Low for HDAO, HBAO+ and GTAO. High remains the default in every preset; SSAO is unchanged. Medium and Low are explicit quality tradeoffs, not pixel-equivalent optimizations. Parser aliases, invalid values, per-key provenance, the shipped default, native menu enum and frame-live settings update are covered by the settings tests. Changing method or quality refreshes only the AO contact shader.

The new variants retain the existing 99 AO assertions and 48 grazing assertions for each relevant quality on native D3D9 and DXVK 3. The original High payloads remain unchanged in the initial quality-selection commit. No game was launched and no duration or FPS benchmark was run. The independent branch retains its own GTAO/silhouette and blur implementations; HDR and automatic materials remain separate features.

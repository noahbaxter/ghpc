# Fifty easiest functions

Straight off `data/ranked.tsv`, which `tools/rank.py` regenerates. Lower score
is easier. The score is a weighted sum, not a count of anything, so a negative
value just means the symbol-shape credits outweighed a one instruction body.

Note what the top of this list actually is: nearly all of these are a single
`jr $ra` with an empty or one instruction delay slot, which is to say empty
virtual overrides and one line accessors. That is a real property of the binary,
not a defect in the score. GH2 declares large abstract interfaces (Stream, Mic,
Synth, BeatMatchSink) and ships inert default implementations for most of them.

| # | score | insns | addr | function | shape |
|---|-------|-------|------|----------|-------|
| 1 | -11.7 | 1 | 0x3c53f0 | Rnd::SetDepthOfField(RndCam *, float, float, float, float) | accessor |
| 2 | -11.2 | 1 | 0x3eb120 | StreamNull::SetJump(float, float, char * const) | accessor |
| 3 | -10.7 | 1 | 0x3ee818 | BeatMaster::AddMultiGem(int, GameGem & const) | accessor |
| 4 | -10.7 | 1 | 0x3ee820 | BeatMaster::AddPhrase(int, Phrase & const) | accessor |
| 5 | -10.7 | 1 | 0x371c28 | MultiplayerAnalyzer::AddPhrase(int, Phrase & const) | accessor |
| 6 | -10.7 | 1 | 0x371c20 | MultiplayerAnalyzer::AddTrack(int, ?) | accessor |
| 7 | -10.7 | 1 | 0x3e84c8 | Stream::SetADSR(int, ADSR & const) | accessor |
| 8 | -10.7 | 1 | 0x36c788 | PlayerMatcher::SetCurrentPhrase(int, PhraseInfo & const) | accessor |
| 9 | -10.7 | 1 | 0x3ee610 | BeatMatchAudio::SetCurrentPhrase(int, PhraseInfo & const) | accessor |
| 10 | -10.7 | 1 | 0x3eb110 | StreamNull::SetFXCore(int, FXCore) | accessor |
| 11 | -10.7 | 1 | 0x3e9110 | Synth::SetFXDelay(int, float) | accessor |
| 12 | -10.7 | 1 | 0x3e9128 | Synth::SetFXFeedback(int, float) | accessor |
| 13 | -10.7 | 1 | 0x3e90e8 | Synth::SetFXMode(int, FXMode) | accessor |
| 14 | -10.7 | 1 | 0x3e90f8 | Synth::SetFXVolume(int, float) | accessor |
| 15 | -10.7 | 1 | 0x3eb100 | StreamNull::SetFX(int, bool) | accessor |
| 16 | -10.7 | 1 | 0x4247b8 | CrowdMeter::SetFrame(float, float) | accessor |
| 17 | -10.7 | 1 | 0x420468 | BandStarMeter::SetFrame(float, float) | accessor |
| 18 | -10.7 | 1 | 0x4201a0 | BandScoreDisplay::SetFrame(float, float) | accessor |
| 19 | -10.7 | 1 | 0x420e18 | BandStreakDisplay::SetFrame(float, float) | accessor |
| 20 | -10.7 | 1 | 0x2b7610 | WorldFx::SetFrame(float, float) | accessor |
| 21 | -10.7 | 1 | 0x3eb0e8 | StreamNull::SetPan(int, float) | accessor |
| 22 | -10.7 | 1 | 0x3c53e8 | Rnd::SetShadowMap(RndTex *, RndCam *) | accessor |
| 23 | -10.7 | 1 | 0x3eb138 | StreamNull::SetSlipOffset(int, float) | accessor |
| 24 | -10.7 | 1 | 0x3eb158 | StreamNull::SetSlipSpeed(int, float) | accessor |
| 25 | -10.7 | 1 | 0x3e84d0 | Stream::SetStereoPair(int, int) | accessor |
| 26 | -10.7 | 1 | 0x3eb0d0 | StreamNull::SetVolume(int, float) | accessor |
| 27 | -10.3 | 2 | 0x36c6c8 | BeatMatchSink::GetNewTrack(int, int) | accessor |
| 28 | -10.2 | 1 | 0x3e84d8 | Stream::AddVirtualChannels(int) | accessor |
| 29 | -10.2 | 1 | 0x42cea0 | ObjectDir::AddedObject(Hmx::Object *) | accessor |
| 30 | -10.2 | 1 | 0x37d3d8 | HelpBarElement::SetColor(Hmx::Color & const) | accessor |
| 31 | -10.2 | 1 | 0x3e93a8 | MicNull::SetCompressorParam(float) | accessor |
| 32 | -10.2 | 1 | 0x3e9398 | MicNull::SetCompressor(bool) | accessor |
| 33 | -10.2 | 1 | 0x3ef490 | BeatMatchControllerSink::SetController(BeatMatchController *) | accessor |
| 34 | -10.2 | 1 | 0x3e9348 | MicNull::SetDMA(bool) | accessor |
| 35 | -10.2 | 1 | 0x3e9380 | MicNull::SetEarpieceVolume(float) | accessor |
| 36 | -10.2 | 1 | 0x3e9370 | MicNull::SetEarpiece(bool) | accessor |
| 37 | -10.2 | 1 | 0x3e9140 | Synth::SetFXChain(bool) | accessor |
| 38 | -10.2 | 1 | 0x3e84e8 | Stream::SetFilter(Stream::Filter *) | accessor |
| 39 | -10.2 | 1 | 0x41d778 | BandLeadMeter::SetFrame(float) | accessor |
| 40 | -10.2 | 1 | 0x3e9358 | MicNull::SetGain(float) | accessor |
| 41 | -10.2 | 1 | 0x236390 | RndMeshAnim::SetKey(float) | accessor |
| 42 | -10.2 | 1 | 0x3b21d0 | RndAnimatable::SetKey(float) | accessor |
| 43 | -10.2 | 1 | 0x3e9150 | Synth::SetMicFX(bool) | accessor |
| 44 | -10.2 | 1 | 0x3e9160 | Synth::SetMicVolume(float) | accessor |
| 45 | -10.2 | 1 | 0x3e4bc0 | WaitSeqInst::SetPan(float) | accessor |
| 46 | -10.2 | 1 | 0x3eacc0 | SampleInst::SetStartProgress(float) | accessor |
| 47 | -10.2 | 1 | 0x3e4bc8 | WaitSeqInst::SetTranspose(float) | accessor |
| 48 | -10.2 | 1 | 0x3e4bb8 | WaitSeqInst::SetVolume(float) | accessor |
| 49 | -9.9 | 3 | 0x3d03c8 | KerningTable::Hash(int, int) | accessor |
| 50 | -9.8 | 2 | 0x2a0478 | OC3Ent::Face::FxCurrentTimeNode::AddInputLink(OC3Ent::Face::FxFaceGraphNodeLink & const) | accessor |

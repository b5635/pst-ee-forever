### CPsSpellAbyssalFury
funcs: AIUpdate_void_, CPsSpellAbyssalFury_, CPsSpellAbyssalFury_void_, Fire_CGameArea_int_int_CPoint_int_short_, Render_CGameArea_CVidMode_
resrefs: ABYSS02 ABYSS03 ABYSS04 ABYSS05 ABYSS06 ABYSS07 S077BARM S077CRVS S077SCRT S077SPT2 SPWI912
effects: Death
calls: CDerivedStats::GetSpellState, CGameArea::GetAllInRange, CGameArea::GetHeightOffset, CGameEffect::CGameEffect, CGameEffectDamage::CheckForIncreasedSpellLevel, CGameEffectDeath::CGameEffectDeath, CGameSprite::DropPath, CGameSprite::DropSearchRequest, CGameSprite::ExtractColorData, CGameSprite::GetCasterLevel, CGameSprite::JumpToPoint, CGameSprite::SetFacing, CGameSprite::SetSequence, CPlanescape::SendHitTrigger, CPsSpellAbyssalFury::AIUpdate, CPsSpellAbyssalFury::CPsSpellAbyssalFury, CPsSpellAbyssalFury::Fire, CPsSpellAbyssalFury::Render

### CPsSpellAxOfTorment
funcs: AIUpdate_void_, CPsSpellAxOfTorment_, CPsSpellAxOfTorment_short_CAIObjectType_CResRef_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: AXOFT03 AXOFT04 AXOFT05 AXOFT06 MAGIC01 S031GRRN S031LSRN SE10FIZZ
effects: CGameEffect::DecodeEffect
calls: CGameArea::SetScreenFlash, CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CGameSprite::AddBlood, CGameSprite::SetSequence, CPlanescape::SendHitTrigger, CPsSpellAxOfTorment::AIUpdate, CPsSpellAxOfTorment::CPsSpellAxOfTorment, CPsSpellAxOfTorment::Fire

### CPsSpellBallLightning
funcs: CPsSpellBallLightning_, CPsSpellBallLightning_short_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: BALLL02 S034BALL
effects: 
calls: CPsSpellBallLightning::CPsSpellBallLightning, CPsSpellBallLightning::Fire

### CPsSpellBlacksphere
funcs: AIUpdate_void_, CPsSpellBlacksphere_, CPsSpellBlacksphere_CResRef_int_uint_int_uchar_
resrefs: BLACK03 BLACK05 S041CAST
effects: 
calls: CPsSpellBlacksphere::AIUpdate, CPsSpellBlacksphere::CPsSpellBlacksphere

### CPsSpellBladestorm
funcs: AIUpdate_void_, CPsSpellBladestorm_, CPsSpellBladestorm_short_CGameAIBase_CResRef_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: BLADE02 BLADE5a BLADE5b BLADE5c EF01TPRT S057BACK S057BLAD S057FLSH
effects: 
calls: CGameArea::GetAllInRange, CGameArea::SetBackgroundBmp, CGameArea::SetScreenFlash, CGameEffect::CGameEffect, CGameEffectDamage::CheckForIncreasedSpellLevel, CGameSprite::GetDirection, CPlanescape::ProjectileRollToHit, CPlanescape::SendHitTrigger, CPsSpellBladestorm::AIUpdate, CPsSpellBladestorm::CPsSpellBladestorm, CPsSpellBladestorm::Fire

### CPsSpellCelestialHost
funcs: AIUpdate_void_, CPsSpellCelestialHost_, CPsSpellCelestialHost_CGameAIBase_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: CELH102 CELH103 CELH104 CELH203 CELH204 CELH205 CELH302 CELH303 CELH304 CELH306 CELH405 CELH406 DBOLT03 DRAGON MAGIC01 MOP504 S091APPR S091ARRS S091ASUN S091BLST S091DEVA S091DVAT S091GATE S091GRLY S091HTCH S091PHNX S091RAIL S091SMHT SE10FIZZ TORMENT
effects: 
calls: CGameArea::GetAllInRange, CGameArea::SetBackgroundBmp, CGameArea::SetScreenFlash, CGameSprite::DropPath, CGameSprite::DropSearchRequest, CGameSprite::SetFacing, CGameSprite::SetSequence, CProjectile::LoadEffects, CPsSpellCelestialHost::AIUpdate, CPsSpellCelestialHost::CPsSpellCelestialHost, CPsSpellCelestialHost::Fire

### CPsSpellChainLightningStorm
funcs: AIUpdate_void_, CPsSpellChainLightningStorm_, CPsSpellChainLightningStorm_CAIObjectType_, Fire_CGameArea_int_int_CPoint_int_short_, Render_CGameArea_CVidMode_
resrefs: CHAIN03 CHAIN04 CHAIN05 MAGIC01 S064BGLT S064HGLT S064LORT S064UPLT SE10FIZZ
effects: 
calls: CGameArea::GetAllInRange, CPsSpellChainLightningStorm::AIUpdate, CPsSpellChainLightningStorm::CPsSpellChainLightningStorm, CPsSpellChainLightningStorm::Fire, CPsSpellChainLightningStorm::Render

### CPsSpellCloudKill
funcs: AIUpdate_void_, CPsSpellCloudKill_, CPsSpellCloudKill_CGameSprite_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: CLOUD03 S056CKLL TRAVEL
effects: 
calls: CGameArea::GetAllInRange, CGameSprite::GetDirection, CPsSpellCloudKill::AIUpdate, CPsSpellCloudKill::CPsSpellCloudKill, CPsSpellCloudKill::Fire

### CPsSpellConflagration
funcs: AIUpdate_void_, CPsSpellConflagration_, CPsSpellConflagration_CResRef_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: A010SPIN AURA_11A CONFL05 CONFLAG
effects: CGameEffect::DecodeEffect
calls: CGameArea::GetAllInRange, CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CGameSprite::DropPath, CGameSprite::DropSearchRequest, CGameSprite::MoveToBack, CGameSprite::MoveToFront, CGameSprite::SetSequence, CProjectile::LoadEffects, CPsSpellConflagration::AIUpdate, CPsSpellConflagration::CPsSpellConflagration, CPsSpellConflagration::Fire

### CPsSpellDeathBolt
funcs: AIUpdate_void_, CPsSpellDeathBolt_, CPsSpellDeathBolt_CResRef_, Fire_CGameArea_int_int_CPoint_int_short_, Render_CGameArea_CVidMode_
resrefs: CHAIN03 DBOLT03 DBOLT05 DBOLT06 S083BGCL S083CLDS S083FLSH
effects: 
calls: CGameArea::GetAllInRange, CGameSprite::DropPath, CGameSprite::DropSearchRequest, CPsSpellDeathBolt::AIUpdate, CPsSpellDeathBolt::CPsSpellDeathBolt, CPsSpellDeathBolt::Fire, CPsSpellDeathBolt::Render

### CPsSpellDesertHell
funcs: AIUpdate_void_, CPsSpellDesertHell_, CPsSpellDesertHell_CAIObjectType_CResRef_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: DESER03 EF01TPRT MAGIC02 S055DSRT
effects: 
calls: CGameArea::GetAllInRange, CGameArea::SetBackgroundBmp, CPsSpellDesertHell::AIUpdate, CPsSpellDesertHell::CPsSpellDesertHell, CPsSpellDesertHell::Fire

### CPsSpellElysiumsFires
funcs: AIUpdate_void_, CPsSpellElysiumsFires_, CPsSpellElysiumsFires_uchar_uchar_uchar_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: S095PROJ
effects: 
calls: CProjectileArea::SetTriggerRange, CPsSpellElysiumsFires::AIUpdate, CPsSpellElysiumsFires::CPsSpellElysiumsFires, CPsSpellElysiumsFires::Fire

### CPsSpellElysiumsTears
funcs: AIUpdate_void_, CPsSpellElysiumsTears_, CPsSpellElysiumsTears_short_uchar_uchar_uchar_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: S070BOMB
effects: 
calls: CPsSpellElysiumsTears::AIUpdate, CPsSpellElysiumsTears::CPsSpellElysiumsTears, CPsSpellElysiumsTears::Fire, CPsSpellElysiumsTearsMeteor::CPsSpellElysiumsTearsMeteor

### CPsSpellElysiumsTearsMeteor
funcs: AIUpdate_void_, CPsSpellElysiumsTearsMeteor_, CPsSpellElysiumsTearsMeteor_short_int_CResRef_uchar_uchar_uchar_
resrefs: ELYST04 ELYST05 MAGIC01 S070BOOM SE10FIZZ
effects: 
calls: CGameArea::GetAllInRange, CGameArea::SetScreenFlash, CGameEffect::CGameEffect, CGameEffectDamage::CheckForIncreasedSpellLevel, CPlanescape::ProjectileRollToHit, CPsSpellElysiumsTearsMeteor::AIUpdate, CPsSpellElysiumsTearsMeteor::CPsSpellElysiumsTearsMeteor

### CPsSpellEmbalming
funcs: CPsSpellEmbalming_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: MAGIC01 SE10FIZZ
effects: 
calls: CPsSpellEmbalming::Fire

### CPsSpellFireAndIce
funcs: AIUpdate_void_, CPsSpellFireAndIce_, CPsSpellFireAndIce_void_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: FIREA02 FIREA03 FIREA04 FIREA05 FIREA06 FIREA07 S056FEXP S056FRBL S056FTRL S056ICBL S056ICBM S056ITRL
effects: CGameEffect::DecodeEffect
calls: CGameArea::GetAllInRange, CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CPlanescape::SendHitTrigger, CPsSpellFireAndIce::AIUpdate, CPsSpellFireAndIce::CPsSpellFireAndIce, CPsSpellFireAndIce::Fire

### CPsSpellForceMissile
funcs: AIUpdate_void_, CPsSpellForceMissile_, CPsSpellForceMissile_int_CResRef_int_int_int_, CallBack_void_, Fire_CGameArea_int_int_CPoint_int_short_, TransferEffects_int_int_CPoint_
resrefs: FORCE03 FORCE05 SPWI405 SPWI410
effects: CGameEffect::DecodeEffect
calls: CGameArea::GetAllInRange, CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CGameSprite::GetCasterLevel, CGameSprite::GetDirection, CGameSprite::SetDirection, CPlanescape::SendHitTrigger, CPsSpellForceMissile::AIUpdate, CPsSpellForceMissile::CPsSpellForceMissile, CPsSpellForceMissile::CallBack, CPsSpellForceMissile::Fire, CPsSpellForceMissile::TransferEffects, CSpell::Demand, CSpell::GetAbility, CSpell::GetAbilityCount, CSpell::GetAbilityEffect, CSpell::GetCasterType

### CPsSpellGlobeOfInvulnerability
funcs: AIUpdate_void_, CPsSpellGlobeOfInvulnerability_, CPsSpellGlobeOfInvulnerability_short_CResRef_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: GLOBE03 GLOBE04 GLOBE05 S034DSPR SHIEL06
effects: 
calls: CGameArea::GetAllInRange, CPsSpellGlobeOfInvulnerability::AIUpdate, CPsSpellGlobeOfInvulnerability::CPsSpellGlobeOfInvulnerability, CPsSpellGlobeOfInvulnerability::Fire

### CPsSpellHoldUndead
funcs: AreaEffect_uchar_, CPsSpellHoldUndead_, CPsSpellHoldUndead_short_CResRef_uchar_
resrefs: HOLDU03 HOLDU05
effects: CGameEffect::DecodeEffect
calls: CGameArea::GetAllInRange, CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CPsSpellHoldUndead::AreaEffect, CPsSpellHoldUndead::CPsSpellHoldUndead

### CPsSpellHowlOfPandemonium
funcs: AIUpdate_void_, CPsSpellHowlOfPandemonium_, CPsSpellHowlOfPandemonium_short_uchar_CResRef_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: HOWL03 HOWL04 S055CAST SPWI603
effects: CGameEffect::DecodeEffect
calls: CGameArea::GetAllInRange, CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CPsSpellHowlOfPandemonium::AIUpdate, CPsSpellHowlOfPandemonium::CPsSpellHowlOfPandemonium, CPsSpellHowlOfPandemonium::Fire

### CPsSpellIceKnife
funcs: AIUpdate_void_, CPsSpellIceKnife_, CPsSpellIceKnife_int_CResRef_, CheckForTargets_int_, Fire_CGameArea_int_int_CPoint_int_short_, OnArrival_void_, TransferEffects_int_int_CPoint_
resrefs: ICEKN04 ICEKN05 ICEKN06 NEG S025COLD S025MELT S025PTRZ SPWI207
effects: CGameEffect::DecodeEffect
calls: CGameArea::GetAllInRange, CGameEffect::CGameEffect, CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CGameEffectDamage::CheckForIncreasedSpellLevel, CGameSprite::GetCasterLevel, CGameSprite::GetDirection, CGameSprite::SetDirection, CPlanescape::ProjectileRollToHit, CPlanescape::SendHitTrigger, CPsSpellIceKnife::AIUpdate, CPsSpellIceKnife::CPsSpellIceKnife, CPsSpellIceKnife::CheckForTargets, CPsSpellIceKnife::Fire, CPsSpellIceKnife::OnArrival, CPsSpellIceKnife::TransferEffects, CSpell::GetAbility, CSpell::GetAbilityCount, CSpell::GetAbilityEffect, CSpell::GetCasterType

### CPsSpellIgnusFury
funcs: AIUpdate_void_, CPsSpellIgnusFury_, CPsSpellIgnusFury_CAIObjectType_short_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: IGNFU05 S084IGHD
effects: CGameEffect::DecodeEffect
calls: CGameArea::GetAllInRange, CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CPsSpellIgnusFury::AIUpdate, CPsSpellIgnusFury::CPsSpellIgnusFury, CPsSpellIgnusFury::Fire

### CPsSpellImprovedStrength
funcs: CPsSpellImprovedStrength_, CPsSpellImprovedStrength_short_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: MAGIC02 S046ISTM SPWI406
effects: CGameEffect::DecodeEffect
calls: CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CGameSprite::FeedBack, CPsSpellImprovedStrength::CPsSpellImprovedStrength, CPsSpellImprovedStrength::Fire

### CPsSpellMeteorStormBombardment
funcs: AIUpdate_void_, CPsSpellMeteorStormBombardment_, CPsSpellMeteorStormBombardment_CAIObjectType_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: METEO02 METEO05 METEO06 METEOR S061BEAM S061ROCK
effects: 
calls: CGameArea::GetAllInRange, CPsSpellMeteorStormBombardment::AIUpdate, CPsSpellMeteorStormBombardment::CPsSpellMeteorStormBombardment, CPsSpellMeteorStormBombardment::Fire

### CPsSpellMissileOfPatience
funcs: AIUpdate_void_, CPsSpellMissileOfPatience_, CPsSpellMissileOfPatience_short_, Fire_CGameArea_int_int_CPoint_int_short_, OnArrival_void_, RemoveSelf_void_, TransferEffects_int_int_CPoint_
resrefs: MAGIC01 MOP103 MOP105 MOP503 MOP504 MOP505 MOP902 MOP903 MOP904 MOP905 S113BLAP S113BLAR S113BLDP S113BLFR S113BOWF S113BWAR S113SLBL S113SLNG S113SMOK SE10FIZZ
effects: 
calls: CGameSprite::GetDirection, CPsSpellMissileOfPatience::AIUpdate, CPsSpellMissileOfPatience::CPsSpellMissileOfPatience, CPsSpellMissileOfPatience::Fire, CPsSpellMissileOfPatience::OnArrival, CPsSpellMissileOfPatience::RemoveSelf, CPsSpellMissileOfPatience::TransferEffects

### CPsSpellPacify
funcs: AIUpdate_void_, CPsSpellPacify_, CPsSpellPacify_short_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: I001ZZZZ MAGIC01 PACIF05 SE10FIZZ TRAVEL
effects: CGameEffect::DecodeEffect
calls: CGameArea::GetAllInRange, CGameEffect::CGameEffect, CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CProjectileArea::CProjectileArea, CPsSpellPacify::AIUpdate, CPsSpellPacify::CPsSpellPacify, CPsSpellPacify::Fire

### CPsSpellPowerOfOne
funcs: CPsSpellPowerOfOne_, CPsSpellPowerOfOne_short_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: MAGIC02 SPWI219
effects: CGameEffect::DecodeEffect
calls: CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CGameSprite::FeedBack, CPsSpellPowerOfOne::CPsSpellPowerOfOne, CPsSpellPowerOfOne::Fire

### CPsSpellPowerWordBlind
funcs: AIUpdate_void_, CPsSpellPowerWordBlind_, CPsSpellPowerWordBlind_uchar_CResRef_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: PWBLI03 PWBLI05 SPWI805
effects: CGameEffect::DecodeEffect
calls: CGameArea::GetAllInRange, CGameArea::SetScreenFlash, CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CPlanescape::SendHitTrigger, CPsSpellPowerWordBlind::AIUpdate, CPsSpellPowerWordBlind::CPsSpellPowerWordBlind, CPsSpellPowerWordBlind::Fire

### CPsSpellPowerWordKill
funcs: AIUpdate_void_, CPsSpellPowerWordKill_, CPsSpellPowerWordKill_CResRef_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: MAGIC01 PWKIL04 PWKIL05 SE10FIZZ
effects: 
calls: CPlanescape::SendHitTrigger, CPsSpellPowerWordKill::AIUpdate, CPsSpellPowerWordKill::CPsSpellPowerWordKill, CPsSpellPowerWordKill::Fire

### CPsSpellRuneOfTorment
funcs: AIUpdate_void_, CPsSpellRuneOfTorment_, CPsSpellRuneOfTorment_CAIObjectType_CResRef_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: CELH406 RUNE RUNET03 RUNET04 RUNET05 RUNET06 S914BGBL S914HGBL S914RUNE TRAVEL
effects: 
calls: CGameArea::GetAllInRange, CPsSpellRuneOfTorment::AIUpdate, CPsSpellRuneOfTorment::CPsSpellRuneOfTorment, CPsSpellRuneOfTorment::Fire

### CPsSpellShroudOfShadows
funcs: AIUpdate_void_, CPsSpellShroudOfShadows_, CPsSpellShroudOfShadows_CResRef_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: S048CRTR SHROU03 SHROU05
effects: CGameEffect::DecodeEffect
calls: CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CGameSprite::GetDirection, CPsSpellShroudOfShadows::AIUpdate, CPsSpellShroudOfShadows::CPsSpellShroudOfShadows, CPsSpellShroudOfShadows::Fire

### CPsSpellStrength
funcs: CPsSpellStrength_, CPsSpellStrength_short_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: MAGIC02 SPWI211
effects: CGameEffect::DecodeEffect
calls: CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CGameSprite::FeedBack, CPsSpellStrength::CPsSpellStrength, CPsSpellStrength::Fire

### CPsSpellStygianIceStorm
funcs: AIUpdate_void_, CPsSpellStygianIceStorm_, CPsSpellStygianIceStorm_CAIObjectType_CResRef_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: SPWI705 STYGI03 STYGI04
effects: CGameEffect::DecodeEffect
calls: CGameArea::GetAllInRange, CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CPsSpellStygianIceStorm::AIUpdate, CPsSpellStygianIceStorm::CPsSpellStygianIceStorm, CPsSpellStygianIceStorm::Fire

### CPsSpellSwarmCurse
funcs: AIUpdate_void_, CPsSpellSwarmCurse_, CPsSpellSwarmCurse_CResRef_short_CGameAIBase_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: NEG S030SWEX SPWI212 SWARM03 SWARM04 SWARM05 TRAVEL
effects: CGameEffect::DecodeEffect
calls: CGameArea::GetAllInRange, CGameArea::SetScreenFlash, CGameEffect::CGameEffect, CGameEffect::CheckAdd, CGameEffect::ClearItemEffect, CGameEffect::DecodeEffect, CGameEffectDamage::CheckForIncreasedSpellLevel, CPlanescape::SendHitTrigger, CProjectileArea::CProjectileArea, CPsSpellSwarmCurse::AIUpdate, CPsSpellSwarmCurse::CPsSpellSwarmCurse, CPsSpellSwarmCurse::Fire

### CPsSpellTashasLaughter
funcs: AreaEffect_uchar_, CPsSpellTashasLaughter_, CPsSpellTashasLaughter_uchar_
resrefs: TRAVEL
effects: 
calls: CGameArea::GetAllInRange, CProjectileArea::CProjectileArea, CPsSpellTashasLaughter::AreaEffect, CPsSpellTashasLaughter::CPsSpellTashasLaughter

### CPsSpellVampiricTouch
funcs: CPsSpellVampiricTouch_, Fire_CGameArea_int_int_CPoint_int_short_
resrefs: MAGIC01 SE10FIZZ
effects: 
calls: CPlanescape::ProjectileRollToHit, CPsSpellVampiricTouch::Fire

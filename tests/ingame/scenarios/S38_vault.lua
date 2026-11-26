-- scenario: save=000700103 env=PSTF_TBC_AUTOPARTY=1 timeout=500
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S38 Vault 9 guard (0510VALT, AR0510): attacked -> GLOBAL Vault_Attack = 1, Dialogue([EA=2]).
S.talker{name = 'VAULT9', expect = {{'Vault_Attack', 'GLOBAL', 1}}}

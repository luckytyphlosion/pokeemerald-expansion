DewfordTown_House1_MapScripts:: @ 81FC3CD
	.byte 0

DewfordTown_House1_EventScript_Man:: @ 81FC3CE
	special HealPlayerParty
	msgbox DewfordTown_House1_Text_Options1, MSGBOX_YESNO
	compare VAR_RESULT, YES
	setvar VAR_0x8005, 0
	goto_if_eq DewfordTown_House1_EventScript_Man_MirrorParty
	msgbox DewfordTown_House1_Text_Options2, MSGBOX_YESNO
	compare VAR_RESULT, YES
	setvar VAR_0x8005, 1
	goto_if_eq DewfordTown_House1_EventScript_Man_UsePrevious
	setvar VAR_0x8005, 2
DewfordTown_House1_EventScript_Man_MirrorParty:
DewfordTown_House1_EventScript_Man_UsePrevious:
	setvar VAR_0x8004, SPECIAL_TEST_AI
	special DoSpecialTrainerBattle
	//trainerbattle_single TRAINER_RICK, Route102_Text_RickIntro, Route102_Text_RickDefeated
	//msgbox DewfordTown_House1_Text_LotToBeSaidForLivingOnIsland, MSGBOX_NPC
	end

DewfordTown_House1_EventScript_Woman:: @ 81FC3D7
	callnative GivePlayerRandomParty
	//msgbox DewfordTown_House1_Text_LifeGoesSlowlyOnIsland, MSGBOX_NPC
	end

DewfordTown_House1_EventScript_Zigzagoon:: @ 81FC3E0
	lock
	faceplayer
	waitse
	playmoncry SPECIES_ZIGZAGOON, 0
	msgbox DewfordTown_House1_Text_Zigzagoon, MSGBOX_DEFAULT
	waitmoncry
	release
	end

DewfordTown_House1_Text_LotToBeSaidForLivingOnIsland: @ 81FC3F3
	.string "There's a lot to be said for living on\n"
	.string "a small island like this in harmony with\l"
	.string "POKéMON and the family.$"

DewfordTown_House1_Text_LifeGoesSlowlyOnIsland: @ 81FC45B
	.string "I left the major port of SLATEPORT\n"
	.string "CITY when I married my husband here.\p"
	.string "Life goes by slowly on this little\n"
	.string "island. But being surrounded by the\l"
	.string "beautiful sea--that's happiness, too.$"

DewfordTown_House1_Text_Zigzagoon: @ 81FC510
	.string "ZIGZAGOON: Guguuh!$"

DewfordTown_House1_Text_Options1:
	.string "Yes: Mirror Party\n"
	.string "No: More options.$"

DewfordTown_House1_Text_Options2:
	.string "Yes: Use previous\n"
	.string "No: Generate new.$"

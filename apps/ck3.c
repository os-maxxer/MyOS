/* Crusader Kings III - a Solis chronicle.
 *
 * A compact feudal grand-strategy game in the spirit of the Paradox classic:
 * a map of Europe divided into provinces held by rival dynasties, a ruler
 * with gold, prestige and piety, wars fought over single provinces with a
 * war score, province upgrades, a pilgrimage, heirs and succession, and
 * random court events. Everything is turn based: one key advances a month.
 *
 * The map is real geography. Every outline is written in tenths of a degree
 * and projected into the map box by LON()/LAT(), so the coastline follows
 * Iberia, Brittany, the Channel, Jutland, the Baltic, both Scandinavian
 * coasts, the Aegean, Italy and Anatolia instead of a coarse raster. Land is
 * filled from the outline itself, so there is no blocky mask to dilate.
 *
 * No floating point is used anywhere (the kernel is built with -mno-sse and
 * -mno-80387); the projection and the scaling are integer arithmetic.
 */

#include <solis/apps/ck3.h>
#include <solis/graphics.h>

#define PROVINCE_COUNT 20
#define RULER_COUNT    4
#define MAX_DEV        9

/* Map space. 54 degrees of longitude (-12..42) by 38 of latitude (34..72),
 * which puts Gibraltar, the North Cape, Ireland and the Caucasus on the map
 * with the proportions of a school atlas. */
#define MAP_W 1000
#define MAP_H 620

/* Tenths of a degree -> map space. A constant here is a compile-time
 * constant, so the outlines below read as coordinates. */
#define LON(d) ((int16_t)(((d) + 120) * 1000 / 540))
#define LAT(d) ((int16_t)((720 - (d)) * 620 / 380))

#define C_BG       0xFF0D1420
#define C_SEA      0xFF10293B
#define C_LAND     0xFF4C5540
#define C_COAST    0xFF7E8A66
#define C_SEA_LINE 0xFF3A5266
#define C_PANEL    0xFF171A24
#define C_PANEL2   0xFF1F2331
#define C_LINE     0xFF333A4C
#define C_TEXT     0xFFE6EAF2
#define C_DIM      0xFF8A93A6
#define C_GOLD     0xFFE8C063
#define C_PIETY    0xFF9B8CE0
#define C_WAR      0xFFD1594F
#define C_OK       0xFF4FB07A
#define C_SEL      0xFFF2D48A
#define C_HEAD     0xFF6E8FD6
#define C_ACTIVE_BORDER 0xFF3D5A8C

#define T_PLAINS 0
#define T_FOREST 1
#define T_HILLS  2
#define T_MARSH  3

#define SCREEN_MENU 0
#define SCREEN_GAME 1
#define SCREEN_HELP 2

struct geo {
    int16_t lon;    /* tenths of a degree east */
    int16_t lat;    /* tenths of a degree north */
};

struct shape {
    const struct geo *pts;
    int count;
};

/* A province is drawn as one or more pieces of land, cut to the shoreline by
 * the tables further down; `prov` indexes province_defs. */
struct prov_piece {
    uint16_t start;
    uint16_t count;
    uint8_t prov;
};

struct province {
    uint8_t  owner;
    uint8_t  terrain;
    uint8_t  dev;
    uint16_t levy;
};

struct ruler {
    const char *name;
    const char *house;
    int gold;
    int prestige;
    int piety;
    int age;
    int health;
    int heir;
    int heir_age;
};

/* ------------------------------------------------------------------ */
/* The landmass                                                        */
/*                                                                     */
/* The coastline is real: every ring below was clipped from Natural    */
/* Earth 1:50m land to this map box (12.5W..42.5E, 33.5N..72.5N) and   */
/* simplified, then written in tenths of a degree. Iberia, Brittany,   */
/* the Channel, Jutland, both Scandinavian coasts, the Baltic, the     */
/* Black Sea, Italy, Greece, Anatolia and the African shore therefore  */
/* keep their true shape, and every island in the box comes with them. */
/* ------------------------------------------------------------------ */

/* Generated from Natural Earth 1:50m land, clipped to the map box
 * (12.5W..42.5E, 33.5N..72.5N) and simplified. Coordinates are
 * tenths of a degree. */

static const struct geo land_0[] = {
    {180, 593}, {186, 594}, {183, 591}, {170, 587}, {162, 586}, {169, 585},
    {167, 584}, {168, 582}, {165, 571}, {160, 562}, {147, 561}, {142, 558},
    {143, 555}, {142, 554}, {129, 554}, {130, 557}, {125, 563}, {128, 563},
    {127, 564}, {129, 566}, {124, 569}, {112, 584}, {111, 590}, {114, 591},
    {108, 592}, {106, 598}, {102, 590}, {96, 591}, {97, 590}, {82, 581},
    {67, 581}, {66, 581}, {67, 583}, {56, 586}, {56, 590}, {61, 589},
    {64, 590}, {59, 591}, {64, 595}, {52, 592}, {52, 596}, {62, 598},
    {57, 599}, {61, 602}, {65, 604}, {65, 602}, {70, 605}, {62, 603},
    {51, 596}, {52, 601}, {57, 601}, {53, 602}, {51, 604}, {56, 607},
    {51, 606}, {50, 610}, {68, 611}, {70, 610}, {76, 612}, {73, 613},
    {74, 614}, {73, 612}, {66, 613}, {53, 611}, {50, 614}, {53, 615},
    {49, 618}, {67, 619}, {53, 619}, {51, 622}, {66, 624}, {61, 624},
    {64, 626}, {76, 625}, {81, 627}, {67, 627}, {76, 631}, {86, 628},
    {82, 632}, {86, 633}, {84, 635}, {86, 636}, {108, 635}, {107, 636},
    {114, 638}, {112, 639}, {114, 640}, {101, 635}, {96, 638}, {106, 644},
    {115, 647}, {113, 648}, {115, 650}, {125, 651}, {129, 653}, {121, 653},
    {127, 659}, {130, 660}, {128, 661}, {140, 663}, {131, 662}, {132, 666},
    {140, 668}, {137, 669}, {141, 671}, {154, 672}, {144, 673}, {150, 676},
    {156, 673}, {157, 675}, {152, 676}, {153, 678}, {149, 677}, {148, 678},
    {156, 679}, {153, 681}, {160, 682}, {163, 679}, {162, 683}, {176, 684},
    {166, 686}, {181, 692}, {183, 695}, {189, 693}, {186, 695}, {192, 697},
    {197, 698}, {196, 694}, {203, 699}, {203, 696}, {201, 693}, {207, 695},
    {205, 697}, {206, 699}, {214, 700}, {220, 698}, {214, 702}, {227, 704},
    {234, 700}, {234, 702}, {248, 710}, {258, 708}, {253, 706}, {250, 701},
    {265, 709}, {267, 709}, {266, 704}, {270, 705}, {275, 708}, {272, 709},
    {277, 711}, {284, 710}, {279, 707}, {283, 707}, {282, 702}, {288, 709},
    {309, 704}, {303, 701}, {288, 701}, {296, 700}, {297, 697}, {302, 699},
    {315, 697}, {320, 698}, {320, 700}, {330, 697}, {322, 697}, {324, 695},
    {335, 694}, {331, 691}, {337, 693}, {353, 693}, {396, 681}, {398, 682},
    {410, 677}, {414, 671}, {405, 664}, {393, 661}, {379, 661}, {355, 664},
    {332, 668}, {329, 671}, {319, 672}, {337, 664}, {334, 663}, {347, 660},
    {348, 658}, {344, 654}, {350, 644}, {374, 638}, {381, 641}, {380, 643},
    {372, 644}, {366, 648}, {369, 652}, {398, 646}, {404, 648}, {398, 653},
    {398, 656}, {422, 665}, {425, 665}, {425, 335}, {-80, 335}, {-69, 340},
    {-59, 358}, {-54, 359}, {-51, 355}, {-46, 352}, {-34, 352}, {-30, 354},
    {-28, 351}, {-19, 351}, {13, 365}, {38, 369}, {48, 369}, {53, 366},
    {65, 371}, {79, 369}, {97, 373}, {98, 371}, {102, 372}, {104, 367},
    {110, 371}, {111, 369}, {105, 362}, {111, 352}, {100, 341}, {102, 339},
    {107, 337}, {107, 335}, {353, 335}, {360, 345}, {358, 363}, {362, 367},
    {361, 369}, {354, 366}, {347, 368}, {337, 362}, {328, 360}, {312, 368},
    {306, 369}, {304, 363}, {297, 362}, {292, 363}, {290, 367}, {275, 367},
    {282, 370}, {273, 370}, {275, 372}, {271, 377}, {272, 380}, {263, 383},
    {264, 386}, {267, 383}, {271, 385}, {268, 387}, {270, 389}, {267, 393},
    {269, 395}, {261, 395}, {262, 400}, {267, 404}, {290, 404}, {288, 405},
    {290, 406}, {298, 408}, {291, 409}, {291, 412}, {313, 411}, {323, 417},
    {334, 420}, {350, 421}, {353, 417}, {361, 417}, {364, 413}, {368, 414},
    {384, 409}, {394, 411}, {403, 410}, {414, 414}, {418, 420}, {414, 427},
    {399, 435}, {387, 443}, {366, 452}, {369, 453}, {367, 454}, {369, 454},
    {372, 453}, {376, 454}, {379, 460}, {385, 461}, {378, 466}, {385, 467},
    {384, 468}, {393, 470}, {392, 473}, {358, 466}, {351, 461}, {352, 464},
    {348, 462}, {350, 457}, {355, 453}, {366, 454}, {364, 451}, {355, 451},
    {339, 444}, {335, 446}, {336, 451}, {325, 454}, {337, 459}, {336, 461},
    {318, 463}, {320, 464}, {316, 466}, {326, 466}, {320, 466}, {318, 472},
    {319, 466}, {316, 468}, {308, 466}, {302, 459}, {296, 457}, {296, 448},
    {290, 448}, {291, 450}, {289, 450}, {286, 435}, {281, 434}, {279, 427},
    {275, 425}, {280, 420}, {282, 416}, {291, 412}, {290, 410}, {275, 410},
    {262, 401}, {263, 403}, {268, 406}, {261, 406}, {251, 410}, {238, 407},
    {239, 404}, {243, 401}, {237, 403}, {239, 400}, {234, 403}, {237, 400},
    {234, 400}, {229, 404}, {229, 406}, {226, 405}, {226, 400}, {233, 392},
    {229, 393}, {231, 390}, {226, 389}, {240, 381}, {240, 377}, {235, 380},
    {230, 379}, {235, 374}, {232, 373}, {227, 375}, {232, 364}, {227, 368},
    {224, 365}, {221, 370}, {219, 367}, {216, 371}, {217, 374}, {211, 379},
    {218, 383}, {229, 380}, {231, 382}, {224, 384}, {215, 383}, {213, 385},
    {212, 383}, {208, 389}, {211, 390}, {208, 390}, {203, 393}, {199, 400},
    {194, 403}, {196, 418}, {186, 424}, {170, 430}, {177, 429}, {169, 434},
    {160, 435}, {152, 442}, {155, 443}, {150, 446}, {149, 451}, {146, 453},
    {143, 453}, {139, 448}, {135, 455}, {138, 456}, {136, 458}, {123, 454},
    {122, 452}, {125, 450}, {122, 447}, {124, 442}, {136, 436}, {140, 427},
    {145, 422}, {152, 419}, {162, 419}, {159, 415}, {180, 407}, {185, 402},
    {183, 398}, {179, 403}, {169, 405}, {165, 397}, {171, 394}, {172, 390},
    {166, 388}, {165, 384}, {161, 379}, {156, 380}, {162, 389}, {157, 400},
    {150, 402}, {148, 407}, {143, 406}, {145, 407}, {140, 408}, {137, 412},
    {131, 412}, {116, 423}, {111, 424}, {105, 430}, {102, 439}, {88, 444},
    {80, 439}, {61, 431}, {41, 436}, {33, 432}, {30, 428}, {33, 423},
    {32, 422}, {32, 419}, {7, 408}, {9, 407}, {6, 406}, {-3, 395}, {-2, 391},
    {2, 388}, {-5, 383}, {-7, 376}, {-16, 374}, {-21, 368}, {-44, 367},
    {-56, 360}, {-60, 362}, {-64, 366}, {-62, 369}, {-64, 368}, {-69, 373},
    {-78, 370}, {-90, 370}, {-88, 374}, {-89, 384}, {-87, 384}, {-92, 384},
    {-93, 387}, {-88, 391}, {-91, 387}, {-95, 388}, {-87, 408}, {-89, 418},
    {-87, 423}, {-88, 426}, {-90, 426}, {-89, 428}, {-92, 430}, {-92, 432},
    {-77, 438}, {-71, 436}, {-58, 436}, {-20, 433}, {-15, 436}, {-11, 447},
    {-12, 447}, {-11, 455}, {-5, 450}, {-8, 455}, {-12, 457}, {-10, 457},
    {-11, 463}, {-18, 465}, {-21, 469}, {-21, 472}, {-17, 472}, {-25, 473},
    {-24, 475}, {-28, 476}, {-43, 478}, {-47, 480}, {-43, 482}, {-46, 483},
    {-42, 483}, {-47, 484}, {-47, 485}, {-32, 488}, {-27, 485}, {-14, 487},
    {-19, 497}, {-13, 497}, {-11, 494}, {4, 494}, {1, 495}, {2, 497},
    {14, 501}, {18, 509}, {34, 514}, {42, 514}, {34, 515}, {43, 515},
    {40, 516}, {42, 516}, {40, 519}, {45, 523}, {48, 529}, {55, 533},
    {68, 534}, {72, 533}, {71, 534}, {72, 537}, {76, 537}, {80, 537},
    {82, 534}, {83, 536}, {85, 534}, {86, 539}, {92, 539}, {98, 536},
    {86, 543}, {90, 545}, {87, 548}, {86, 554}, {81, 556}, {82, 566},
    {87, 565}, {89, 567}, {92, 567}, {93, 570}, {91, 570}, {88, 567},
    {83, 568}, {86, 571}, {94, 572}, {100, 576}, {106, 577}, {103, 566},
    {109, 564}, {103, 562}, {102, 559}, {96, 555}, {97, 553}, {95, 550},
    {100, 547}, {99, 545}, {110, 544}, {109, 540}, {114, 539}, {126, 545},
    {130, 544}, {137, 542}, {140, 538}, {146, 536}, {146, 538}, {139, 539},
    {138, 541}, {144, 539}, {181, 548}, {188, 547}, {184, 547}, {188, 544},
    {194, 544}, {200, 549}, {207, 551}, {211, 556}, {206, 550}, {212, 549},
    {210, 566}, {217, 576}, {226, 577}, {236, 570}, {244, 573}, {243, 578},
    {245, 584}, {238, 584}, {235, 587}, {237, 588}, {234, 589}, {235, 592},
    {255, 596}, {279, 594}, {281, 598}, {283, 597}, {291, 600}, {301, 599},
    {297, 602}, {286, 604}, {285, 607}, {265, 604}, {266, 606}, {258, 603},
    {230, 598}, {232, 600}, {229, 602}, {225, 600}, {226, 604}, {214, 606},
    {214, 610}, {216, 616}, {213, 620}, {213, 623}, {211, 626}, {217, 630},
    {215, 632}, {223, 633}, {222, 634}, {225, 636}, {236, 640}, {246, 648},
    {254, 650}, {253, 655}, {247, 657}, {246, 659}, {224, 659}, {223, 656},
    {214, 653}, {216, 651}, {211, 648}, {215, 645}, {208, 639}, {186, 632},
    {183, 630}, {185, 629}, {179, 629}, {180, 626}, {174, 625}, {176, 622},
    {174, 619}, {175, 617}, {171, 616}, {173, 607}, {180, 606}, {190, 598},
};

static const struct geo land_1[] = {
    {-31, 585}, {-40, 580}, {-39, 578}, {-41, 576}, {-21, 577}, {-18, 575},
    {-26, 566}, {-33, 564}, {-27, 563}, {-38, 561}, {-26, 560}, {-17, 556},
    {-12, 547}, {-1, 541}, {-2, 540}, {1, 536}, {-7, 537}, {3, 533}, {4, 532},
    {0, 529}, {11, 530}, {17, 528}, {16, 521}, {8, 517}, {9, 516}, {4, 515},
    {14, 514}, {14, 512}, {2, 508}, {-14, 509}, {-20, 506}, {-30, 507},
    {-34, 506}, {-37, 502}, {-42, 504}, {-56, 501}, {-42, 512}, {-31, 512},
    {-24, 517}, {-33, 514}, {-44, 517}, {-52, 517}, {-52, 519}, {-42, 523},
    {-40, 525}, {-41, 529}, {-47, 528}, {-43, 531}, {-31, 533}, {-31, 534},
    {-27, 533}, {-31, 535}, {-28, 541}, {-32, 541}, {-36, 546}, {-30, 550},
    {-50, 548}, {-52, 550}, {-47, 555}, {-49, 559}, {-46, 559}, {-48, 562},
    {-52, 559}, {-51, 562}, {-54, 560}, {-56, 554}, {-57, 553}, {-55, 558},
    {-56, 561}, {-52, 568}, {-57, 565}, {-61, 567}, {-57, 569}, {-59, 569},
    {-56, 572}, {-58, 574}, {-56, 575}, {-57, 577}, {-56, 579}, {-52, 579},
    {-54, 581}, {-50, 583}, {-50, 586}, {-33, 586},
};

static const struct geo land_2[] = {
    {-72, 551}, {-61, 552}, {-57, 548}, {-59, 546}, {-55, 545}, {-57, 545},
    {-56, 543}, {-63, 540}, {-60, 529}, {-65, 523}, {-63, 522}, {-84, 519},
    {-84, 517}, {-93, 515}, {-98, 515}, {-96, 517}, {-101, 516}, {-96, 519},
    {-103, 518}, {-99, 521}, {-104, 522}, {-88, 527}, {-99, 526}, {-89, 532},
    {-101, 534}, {-101, 535}, {-97, 536}, {-99, 537}, {-96, 538}, {-99, 539},
    {-99, 541}, {-101, 543}, {-85, 542}, {-81, 546}, {-88, 547}, {-83, 551},
    {-77, 553}, {-77, 550}, {-73, 554}, {-70, 552},
};

static const struct geo land_3[] = {
    {156, 382}, {151, 375}, {153, 371}, {151, 367}, {145, 368}, {124, 378},
    {127, 382}, {138, 380},
};

static const struct geo land_4[] = {
    {96, 409}, {98, 405}, {96, 392}, {91, 392}, {90, 390}, {86, 389},
    {84, 392}, {85, 398}, {82, 409}, {86, 409}, {92, 413},
};

static const struct geo land_5[] = {
    {126, 558}, {122, 555}, {124, 553}, {121, 552}, {121, 548}, {119, 548},
    {110, 557}, {116, 560}, {118, 557}, {119, 560}, {122, 561}, {126, 561},
};

static const struct geo land_6[] = {
    {95, 428}, {96, 421}, {92, 414}, {88, 416}, {86, 424}, {93, 427},
    {94, 430},
};

static const struct geo land_7[] = {
    {345, 356}, {339, 353}, {341, 350}, {330, 346}, {324, 347}, {323, 350},
    {329, 352}, {329, 354},
};

static const struct geo land_8[] = {
    {239, 355}, {257, 353}, {258, 351}, {263, 353}, {262, 350}, {248, 349},
    {245, 352}, {236, 353}, {236, 355},
};

static const struct geo land_9[] = {
    {158, 686}, {162, 688}, {165, 688}, {165, 686}, {160, 684}, {143, 682},
    {154, 686}, {155, 690}, {160, 693}, {161, 692}, {158, 690}, {159, 687},
};

static const struct geo land_10[] = {
    {191, 578}, {188, 577}, {189, 574}, {181, 569}, {183, 571}, {181, 576},
    {185, 578},
};

static const struct geo land_11[] = {
    {226, 586}, {233, 585}, {220, 579}, {222, 582}, {219, 583}, {219, 585},
};

static const struct geo land_12[] = {
    {106, 556}, {108, 553}, {106, 551}, {100, 552}, {99, 554}, {99, 555},
};

static const struct geo land_13[] = {
    {31, 398}, {35, 397}, {31, 393}, {24, 396}, {29, 399}, {32, 400},
};

static const struct geo land_14[] = {
    {234, 390}, {241, 386}, {246, 380}, {229, 388},
};

static const struct geo land_15[] = {
    {175, 696}, {180, 695}, {180, 692}, {168, 691},
};

static const struct geo land_16[] = {
    {193, 701}, {196, 700}, {188, 696}, {181, 696}, {187, 698}, {191, 702},
};

static const struct geo land_17[] = {
    {-62, 584}, {-66, 581}, {-64, 580}, {-71, 578}, {-69, 579}, {-71, 580},
    {-70, 582}, {-67, 582}, {-62, 585},
};

static const struct geo land_18[] = {
    {-61, 575}, {-61, 573}, {-57, 573}, {-59, 570}, {-68, 574}, {-64, 577},
};

static const struct geo land_19[] = {
    {152, 689}, {154, 688}, {152, 686}, {144, 687},
};

static const struct geo land_20[] = {
    {264, 393}, {266, 390}, {265, 390}, {261, 391}, {263, 392}, {258, 392},
};

static const struct geo land_21[] = {
    {229, 588}, {225, 587}, {221, 589}, {226, 591},
};

static const struct geo land_22[] = {
    {-13, 605}, {-11, 604}, {-13, 599}, {-13, 602}, {-17, 603},
};

static const struct geo land_23[] = {
    {137, 544}, {134, 542}, {132, 545}, {134, 547},
};

static const struct geo land_24[] = {
    {114, 549}, {118, 547}, {110, 548}, {111, 549},
};

static const struct geo land_25[] = {
    {-58, 563}, {-63, 563}, {-61, 565}, {-63, 566}, {-61, 566},
};

static const struct geo land_26[] = {
    {200, 604}, {202, 602}, {197, 601}, {197, 603},
};

static const struct geo land_27[] = {
    {-66, 622}, {-68, 621}, {-67, 620}, {-72, 623},
};

static const struct geo land_28[] = {
    {256, 711}, {261, 710}, {256, 710}, {253, 711},
};

static const struct geo land_29[] = {
    {261, 382}, {259, 382}, {258, 386}, {262, 385},
};

static const struct shape lands[] = {
    {land_0, 655},
    {land_1, 84},
    {land_2, 40},
    {land_3, 8},
    {land_4, 11},
    {land_5, 12},
    {land_6, 7},
    {land_7, 8},
    {land_8, 9},
    {land_9, 12},
    {land_10, 7},
    {land_11, 6},
    {land_12, 6},
    {land_13, 6},
    {land_14, 4},
    {land_15, 4},
    {land_16, 6},
    {land_17, 9},
    {land_18, 6},
    {land_19, 4},
    {land_20, 6},
    {land_21, 4},
    {land_22, 5},
    {land_23, 4},
    {land_24, 4},
    {land_25, 5},
    {land_26, 4},
    {land_27, 4},
    {land_28, 4},
    {land_29, 4},
};
#define LAND_COUNT ((int)(sizeof(lands) / sizeof(lands[0])))

/* Sea names, placed where there is water and room for the text. */
static const struct {
    const char *name;
    struct geo at;
} sea_labels[] = {
    {"ATLANTIC",      {-80, 502}},
    {"OCEAN",         {-80, 489}},
    {"NORTH SEA",     {20, 565}},
    {"BALTIC",        {190, 570}},
    {"MEDITERRANEAN", {50, 381}},
    {"BLACK SEA",     {340, 450}},
};

/* ------------------------------------------------------------------ */
/* Provinces                                                           */
/* ------------------------------------------------------------------ */

/* Province land: each province box cut out of the coastline above, so a
 * province never spills past the shore. */
static const struct geo prov_points[] = {
    {-72, 551}, {-63, 552}, {-63, 522}, {-88, 516}, {-98, 517}, {-96, 519},
    {-98, 518}, {-98, 525}, {-88, 527}, {-98, 526}, {-89, 532}, {-98, 533},
    {-96, 538}, {-98, 543}, {-85, 542}, {-81, 546}, {-88, 547}, {-83, 551},
    {-77, 553}, {-77, 550}, {-73, 554}, {-70, 552}, {-31, 585}, {-41, 576},
    {-21, 577}, {-20, 573}, {-26, 566}, {-33, 564}, {-27, 563}, {-38, 561},
    {-21, 559}, {-20, 549}, {-52, 549}, {-47, 555}, {-49, 559}, {-46, 559},
    {-48, 562}, {-52, 559}, {-51, 585}, {-13, 548}, {-1, 541}, {1, 536},
    {-7, 537}, {3, 533}, {0, 529}, {14, 529}, {14, 520}, {4, 515}, {14, 512},
    {2, 508}, {-30, 507}, {-37, 502}, {-42, 504}, {-51, 501}, {-54, 502},
    {-42, 512}, {-31, 512}, {-24, 517}, {-33, 514}, {-52, 517}, {-40, 525},
    {-41, 529}, {-47, 528}, {-43, 531}, {-27, 533}, {-31, 535}, {-29, 542},
    {-36, 545}, {-34, 548}, {-51, 548}, {-42, 533}, {-44, 531}, {-46, 534},
    {-44, 542}, {-48, 541}, {-44, 544}, {118, 545}, {118, 573}, {118, 545},
    {90, 545}, {86, 554}, {81, 556}, {82, 566}, {87, 565}, {93, 570},
    {85, 567}, {83, 569}, {105, 573}, {103, 566}, {109, 564}, {96, 555},
    {95, 550}, {99, 545}, {118, 548}, {110, 557}, {116, 560}, {118, 557},
    {106, 556}, {106, 551}, {99, 554}, {52, 524}, {52, 503}, {25, 503},
    {25, 511}, {42, 514}, {34, 515}, {43, 515}, {39, 518}, {45, 524},
    {70, 524}, {70, 499}, {46, 499}, {46, 524}, {76, 504}, {76, 472},
    {48, 472}, {48, 504}, {114, 540}, {114, 512}, {70, 512}, {72, 537},
    {85, 534}, {86, 539}, {98, 536}, {89, 540}, {114, 539}, {136, 506},
    {136, 473}, {94, 473}, {94, 506}, {16, 502}, {16, 484}, {-18, 484},
    {-14, 487}, {-18, 497}, {-2, 493}, {15, 502}, {52, 499}, {52, 476},
    {14, 476}, {14, 499}, {28, 470}, {28, 436}, {-12, 436}, {-11, 455},
    {-5, 450}, {-12, 457}, {-12, 470}, {64, 453}, {64, 430}, {41, 436},
    {31, 430}, {31, 453}, {-36, 439}, {-36, 424}, {-87, 424}, {-87, 433},
    {-80, 437}, {-43, 434}, {-36, 435}, {-4, 426}, {-4, 394}, {-66, 394},
    {-66, 426}, {20, 424}, {20, 398}, {20, 413}, {10, 411}, {-1, 398},
    {-8, 398}, {-8, 424}, {126, 474}, {126, 459}, {82, 459}, {82, 474},
    {124, 462}, {124, 446}, {124, 455}, {123, 446}, {72, 446}, {72, 462},
    {124, 446}, {124, 424}, {124, 446}, {124, 424}, {111, 424}, {99, 441},
    {99, 446}, {152, 377}, {150, 367}, {125, 377}, {127, 382}, {138, 380},
    {152, 382},
};

static const struct prov_piece prov_pieces[] = {
    {0, 22, 0},   /* Ireland */
    {22, 17, 1},   /* Scotland */
    {39, 31, 2},   /* England */
    {70, 3, 2},   /* England */
    {73, 3, 2},   /* England */
    {76, 17, 3},   /* Denmark */
    {93, 4, 3},   /* Denmark */
    {97, 3, 3},   /* Denmark */
    {100, 9, 4},   /* Flanders */
    {109, 4, 5},   /* Brabant */
    {113, 4, 6},   /* Lotharingia */
    {117, 9, 7},   /* Saxony */
    {126, 4, 8},   /* Bavaria */
    {130, 7, 9},   /* Normandy */
    {137, 4, 10},   /* Ile-de-France */
    {141, 7, 11},   /* Aquitaine */
    {148, 5, 12},   /* Provence */
    {153, 7, 13},   /* Leon */
    {160, 4, 14},   /* Castile */
    {164, 7, 15},   /* Aragon */
    {171, 4, 16},   /* Tirol */
    {175, 6, 17},   /* Lombardy */
    {181, 7, 18},   /* Tuscany */
    {188, 6, 19},   /* Sicily */
};
#define PROV_PIECE_COUNT ((int)(sizeof(prov_pieces) / sizeof(prov_pieces[0])))


/* "label" is the short name used on the map, "name" the full one used in
 * the panel; the land each province covers lives in the piece tables below. */
static const struct {
    const char *name;
    const char *label;
    uint8_t owner;
    uint8_t terrain;
    uint8_t dev;
    struct geo label_at;
} province_defs[PROVINCE_COUNT] = {
    {"Ireland", "IRE", 1, T_FOREST, 3, {-80, 534}},
    {"Scotland", "SCO", 1, T_HILLS, 3, {-36, 568}},
    {"England", "ENG", 1, T_PLAINS, 5, {-18, 524}},
    {"Denmark", "DEN", 1, T_MARSH, 3, {98, 558}},

    {"Flanders", "FLA", 0, T_MARSH, 4, {38, 512}},
    {"Brabant", "BRA", 0, T_PLAINS, 4, {58, 512}},
    {"Lotharingia", "LOR", 0, T_FOREST, 3, {62, 488}},
    {"Saxony", "SAX", 0, T_FOREST, 3, {92, 526}},
    {"Bavaria", "BAV", 3, T_HILLS, 4, {114, 490}},

    {"Normandy", "NOR", 2, T_PLAINS, 4, {-2, 492}},
    {"Ile-de-France", "FRA", 2, T_PLAINS, 5, {33, 488}},
    {"Aquitaine", "AQU", 2, T_FOREST, 4, {6, 452}},
    {"Provence", "PRO", 2, T_PLAINS, 4, {48, 442}},

    {"Leon", "LEO", 2, T_HILLS, 3, {-66, 429}},
    {"Castile", "CAS", 2, T_PLAINS, 4, {-34, 411}},
    {"Aragon", "ARA", 2, T_HILLS, 3, {6, 411}},

    {"Tirol", "TIR", 3, T_HILLS, 4, {104, 468}},
    {"Lombardy", "LOM", 3, T_PLAINS, 5, {98, 456}},
    {"Tuscany", "TUS", 3, T_HILLS, 4, {112, 434}},
    {"Sicily", "SIC", 3, T_PLAINS, 3, {139, 376}},
};

static const char *const ruler_names[RULER_COUNT] = {
    "Alaric", "Otto", "Hugues", "Berthold"
};
static const char *const ruler_houses[RULER_COUNT] = {
    "Solis", "von Loon", "de Metz", "von Tirol"
};
static const char *const terrain_names[4] = {
    "Plains", "Forest", "Hills", "Marsh"
};
static const uint32_t owner_colors[RULER_COUNT] = {
    0xFF4C86D6, 0xFFC25454, 0xFF5E9B63, 0xFF9A64BC
};
static const uint32_t owner_shade[RULER_COUNT] = {
    0xFF2A4368, 0xFF542C2C, 0xFF2E4530, 0xFF422F52
};

static struct province provinces[PROVINCE_COUNT];
static struct ruler rulers[RULER_COUNT];
static struct geo centroids[PROVINCE_COUNT];

static int screen = SCREEN_MENU;
static int menu_sel = 0;
static int started;
static int sel;
static int year;
static int month;
static int war_on;
static int war_target;
static int war_enemy;
static int war_score;
static int event_open;
static int event_kind;
static int game_over;
static int months_to_event;
static uint32_t rng_state;

/* Map transform, set by draw_map and reused by the polygon helper. */
static int map_ox;
static int map_oy;
static int map_scale;

static char chron[6][52];
static int chron_count;

/* ------------------------------------------------------------------ */
/* Small string and random helpers (the apps have no libc)             */
/* ------------------------------------------------------------------ */

static int slen(const char *s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void scopy(char *dst, const char *src, int max) {
    int i = 0;
    if (max <= 0) return;
    while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void sappend(char *dst, const char *src, int max) {
    int i = slen(dst);
    int j = 0;
    if (max <= 0) return;
    while (src && src[j] && i < max - 1) { dst[i++] = src[j++]; }
    dst[i] = '\0';
}

static void snum(char *dst, int value, int max) {
    char tmp[12];
    int n = 0;
    int i = slen(dst);
    if (value < 0) {
        if (i < max - 1) dst[i++] = '-';
        value = -value;
    }
    if (value == 0) tmp[n++] = '0';
    while (value > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (n > 0 && i < max - 1) dst[i++] = tmp[--n];
    dst[i] = '\0';
}

static uint32_t rnd(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 16;
}

static int rnd_below(int n) {
    if (n <= 0) return 0;
    return (int)(rnd() % (uint32_t)n);
}

static int iabs(int v) {
    return v < 0 ? -v : v;
}

/* ------------------------------------------------------------------ */
/* Chronicle                                                           */
/* ------------------------------------------------------------------ */

static void chron_shift(void) {
    if (chron_count < 6) {
        chron_count++;
        return;
    }
    for (int i = 1; i < 6; i++) scopy(chron[i - 1], chron[i], 52);
}

static void chron_msg(const char *text) {
    chron_shift();
    scopy(chron[chron_count - 1], text, 52);
}

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static void province_refresh(int i) {
    static const int levy_bonus[4] = {10, 20, 30, 5};
    provinces[i].levy = (uint16_t)(30 + provinces[i].dev * 22 +
                                   levy_bonus[provinces[i].terrain]);
}

static int province_tax(int i) {
    int tax = 1 + provinces[i].dev / 2;
    if (provinces[i].terrain == T_PLAINS) tax++;
    if (provinces[i].owner == 0 && rulers[0].piety >= 100) tax++;
    return tax;
}

static int realm_levies(void) {
    int total = 0;
    for (int i = 0; i < PROVINCE_COUNT; i++)
        if (provinces[i].owner == 0) total += provinces[i].levy;
    return total;
}

static int realm_provinces(int owner) {
    int total = 0;
    for (int i = 0; i < PROVINCE_COUNT; i++)
        if (provinces[i].owner == owner) total++;
    return total;
}

void ck3_init(void) {
    rng_state = 0x5EED1234u;
    for (int i = 0; i < PROVINCE_COUNT; i++) {
        provinces[i].owner = province_defs[i].owner;
        provinces[i].terrain = province_defs[i].terrain;
        provinces[i].dev = province_defs[i].dev;
        province_refresh(i);

        int lon = 0, lat = 0, n = 0;
        for (int k = 0; k < PROV_PIECE_COUNT; k++) {
            if (prov_pieces[k].prov != i) continue;
            for (int p = 0; p < prov_pieces[k].count; p++) {
                lon += prov_points[prov_pieces[k].start + p].lon;
                lat += prov_points[prov_pieces[k].start + p].lat;
                n++;
            }
        }
        centroids[i].lon = (int16_t)(lon / n);
        centroids[i].lat = (int16_t)(lat / n);
    }
    for (int i = 0; i < RULER_COUNT; i++) {
        rulers[i].name = ruler_names[i];
        rulers[i].house = ruler_houses[i];
        rulers[i].gold = i == 0 ? 120 : 90;
        rulers[i].prestige = i == 0 ? 80 : 60;
        rulers[i].piety = i == 0 ? 30 : 20;
        rulers[i].age = i == 0 ? 24 : 30 + i * 7;
        rulers[i].health = 100;
        rulers[i].heir = 0;
        rulers[i].heir_age = 0;
    }
    sel = 6;                       /* Lotharingia, the player's seat */
    year = 1066;
    month = 1;
    war_on = 0;
    war_target = -1;
    war_enemy = -1;
    war_score = 0;
    event_open = 0;
    event_kind = 0;
    game_over = 0;
    months_to_event = 3 + rnd_below(3);
    chron_count = 0;
    chron_msg("You inherit the duchy of Solis.");
    chron_msg("Rival houses covet your provinces.");
    started = 0;
    screen = SCREEN_MENU;
    menu_sel = 0;
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

struct event_def {
    const char *title;
    const char *text1;
    const char *text2;
    const char *opt1;
    const char *opt2;
};

static const struct event_def events[] = {
    {"A merchant of Brabant",
     "offers a loan at a steep rate.",
     "He waits for your answer.",
     "Take 60 gold, lose prestige",
     "Decline politely"},
    {"The bishop requests",
     "a donation for the new abbey.",
     "He watches your purse.",
     "Give 40 gold for piety",
     "Refuse and lose 5 piety"},
    {"A distant claimant",
     "disputes your title to a province.",
     "Your steward looks worried.",
     "Pay 30 gold to silence him",
     "Ignore him, lose prestige"},
    {"The harvest is rich",
     "and the villages bring tribute.",
     "Your steward counts the carts.",
     "Hold a feast for prestige",
     "Store the grain as gold"},
    {"Your marshal drills",
     "the levies in the courtyard.",
     "The men are willing.",
     "Pay 40 gold, raise levies",
     "Spare the treasury"},
};

#define EVENT_COUNT ((int)(sizeof(events) / sizeof(events[0])))

static void event_apply(int kind, int choice) {
    int i;
    switch (kind) {
        case 0:
            if (choice == 0) {
                rulers[0].gold += 60;
                rulers[0].prestige -= 10;
                chron_msg("You take the merchant's loan.");
            } else {
                chron_msg("You decline the loan.");
            }
            break;
        case 1:
            if (choice == 0) {
                rulers[0].gold -= 40;
                rulers[0].piety += 35;
                chron_msg("The abbey remembers your gift.");
            } else {
                rulers[0].piety -= 5;
                chron_msg("The bishop leaves disappointed.");
            }
            break;
        case 2:
            if (choice == 0) {
                rulers[0].gold -= 30;
                rulers[0].prestige += 15;
                chron_msg("The claimant withdraws his suit.");
            } else {
                rulers[0].prestige -= 20;
                for (i = 0; i < PROVINCE_COUNT; i++)
                    if (provinces[i].owner == 0 && provinces[i].dev > 1) {
                        provinces[i].dev--;
                        province_refresh(i);
                        break;
                    }
                chron_msg("Your title is mocked at court.");
            }
            break;
        case 3:
            if (choice == 0) {
                rulers[0].prestige += 25;
                rulers[0].gold += 10;
                chron_msg("The feast is long remembered.");
            } else {
                rulers[0].gold += 45;
                chron_msg("Granaries fill, coin follows.");
            }
            break;
        default:
            if (choice == 0) {
                rulers[0].gold -= 40;
                for (i = 0; i < PROVINCE_COUNT; i++)
                    if (provinces[i].owner == 0) {
                        provinces[i].dev = (uint8_t)(provinces[i].dev < MAX_DEV
                                                         ? provinces[i].dev + 1
                                                         : provinces[i].dev);
                        province_refresh(i);
                    }
                chron_msg("The levies are drilled and ready.");
            } else {
                chron_msg("You spare the treasury.");
            }
            break;
    }
    if (rulers[0].gold < 0) rulers[0].gold = 0;
    if (rulers[0].piety < 0) rulers[0].piety = 0;
    if (rulers[0].prestige < 0) rulers[0].prestige = 0;
}

/* ------------------------------------------------------------------ */
/* Turn                                                                */
/* ------------------------------------------------------------------ */

static void end_war_win(void) {
    provinces[war_target].owner = 0;
    province_refresh(war_target);
    rulers[0].prestige += 150;
    rulers[0].gold += 25;
    chron_msg("Victory! A province joins your realm.");
    war_on = 0;
    war_target = -1;
    war_enemy = -1;
    war_score = 0;
}

static void end_war_loss(void) {
    rulers[0].prestige = rulers[0].prestige > 120 ? rulers[0].prestige - 120 : 0;
    rulers[0].gold = rulers[0].gold > 50 ? rulers[0].gold - 50 : 0;
    for (int i = 0; i < PROVINCE_COUNT; i++)
        if (provinces[i].owner == 0 && provinces[i].dev > 1) {
            provinces[i].dev--;
            province_refresh(i);
            break;
        }
    chron_msg("Defeat. Your host is scattered.");
    war_on = 0;
    war_target = -1;
    war_enemy = -1;
    war_score = 0;
}

static void player_dies(void) {
    if (rulers[0].heir) {
        chron_msg("You die. Your heir takes the seat.");
        rulers[0].age = 16;
        rulers[0].health = 100;
        rulers[0].heir = 0;
        rulers[0].heir_age = 0;
        rulers[0].prestige = rulers[0].prestige * 4 / 5;
        chron_msg("A new generation rules House Solis.");
    } else {
        game_over = 1;
        chron_msg("You die without an heir.");
        chron_msg("Your dynasty has ended.");
    }
}

static void ai_turn(void) {
    /* A rival may raid your lands when your prestige is low. */
    if (rnd_below(100) < 12) {
        int rival = 1 + rnd_below(RULER_COUNT - 1);
        if (rulers[0].prestige >= 90 && rnd_below(100) < 55) {
            rulers[0].prestige += 10;
            chron_msg("Raiders turned back at the border.");
        } else {
            int loss = 15 + rnd_below(25);
            rulers[0].gold = rulers[0].gold > loss ? rulers[0].gold - loss : 0;
            rulers[0].prestige = rulers[0].prestige > 10
                                     ? rulers[0].prestige - 10 : 0;
            char line[52];
            scopy(line, "Raiders from ", 52);
            sappend(line, rulers[rival].house, 52);
            sappend(line, " burn a village.", 52);
            chron_msg(line);
        }
    }
}

static void advance_month(void) {
    if (game_over || event_open) return;

    month++;
    if (month > 12) { month = 1; year++; }

    /* Taxes */
    int income = 0;
    for (int i = 0; i < PROVINCE_COUNT; i++)
        if (provinces[i].owner == 0) income += province_tax(i);
    rulers[0].gold += income;
    if (rulers[0].gold > 99999) rulers[0].gold = 99999;

    /* War */
    if (war_on) {
        int mine = realm_levies();
        int theirs = provinces[war_target].levy * 2;
        /* A stronger host advances the war score; a weaker one loses ground. */
        int swing = 2 + (mine - theirs) / 12;
        if (swing < -12) swing = -12;
        if (swing > 12) swing = 12;
        war_score += swing;
        if (war_score >= 100) end_war_win();
        else if (war_score <= -100) end_war_loss();
    }

    ai_turn();

    /* Heir grows up */
    if (rulers[0].heir) rulers[0].heir_age++;

    /* Age and mortality */
    if (month == 1) rulers[0].age++;
    int risk = 0;
    if (rulers[0].age > 45) risk = (rulers[0].age - 45) * 2;
    if (rulers[0].health < 60) risk += 4;
    if (risk > 0 && rnd_below(1000) < risk) player_dies();

    /* Court events */
    if (!game_over) {
        months_to_event--;
        if (months_to_event <= 0) {
            event_kind = rnd_below(EVENT_COUNT);
            event_open = 1;
            months_to_event = 4 + rnd_below(4);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Player actions                                                      */
/* ------------------------------------------------------------------ */

static void act_declare_war(void) {
    if (game_over) return;
    if (war_on) { chron_msg("You are already at war."); return; }
    if (provinces[sel].owner == 0) { chron_msg("That province is yours."); return; }
    if (rulers[0].prestige < 25) {
        chron_msg("You need 25 prestige to press a claim.");
        return;
    }
    rulers[0].prestige -= 25;
    war_on = 1;
    war_target = sel;
    war_enemy = provinces[sel].owner;
    war_score = 0;
    char line[52];
    scopy(line, "War declared on ", 52);
    sappend(line, rulers[war_enemy].house, 52);
    chron_msg(line);
}

static void act_build(void) {
    if (provinces[sel].owner != 0) {
        chron_msg("Build only in your own provinces.");
        return;
    }
    if (provinces[sel].dev >= MAX_DEV) { chron_msg("It is fully built."); return; }
    if (rulers[0].gold < 60) { chron_msg("You need 60 gold to build."); return; }
    rulers[0].gold -= 60;
    provinces[sel].dev++;
    province_refresh(sel);
    char line[52];
    scopy(line, "Built up ", 52);
    sappend(line, province_defs[sel].name, 52);
    chron_msg(line);
}

static void act_feast(void) {
    if (rulers[0].gold < 30) { chron_msg("You need 30 gold for a feast."); return; }
    rulers[0].gold -= 30;
    rulers[0].prestige += 25;
    rulers[0].health = rulers[0].health < 95 ? rulers[0].health + 5 : 100;
    chron_msg("Your court feasts and talks of you.");
}

static void act_pilgrimage(void) {
    if (rulers[0].gold < 50) { chron_msg("A pilgrimage costs 50 gold."); return; }
    rulers[0].gold -= 50;
    rulers[0].piety += 40;
    chron_msg("You walk to the shrine and return.");
}

static void act_heir(void) {
    if (rulers[0].heir) { chron_msg("You already have an heir."); return; }
    if (rulers[0].gold < 40) { chron_msg("A marriage costs 40 gold."); return; }
    rulers[0].gold -= 40;
    rulers[0].heir = 1;
    rulers[0].heir_age = 0;
    chron_msg("A child is born to House Solis.");
}

/* Move to the closest province in the pressed direction, measured from the
 * centroids, so the arrows feel geographic on the Europe map. */
static void move_selection(int dx, int dy) {
    int best = -1;
    long best_score = 0;
    for (int i = 0; i < PROVINCE_COUNT; i++) {
        if (i == sel) continue;
        int ddx = (int)centroids[i].lon - (int)centroids[sel].lon;
        int ddy = (int)centroids[i].lat - (int)centroids[sel].lat;
        if (dx > 0 && ddx <= 6) continue;
        if (dx < 0 && ddx >= -6) continue;
        if (dy > 0 && ddy <= 6) continue;
        if (dy < 0 && ddy >= -6) continue;
        long along = dx != 0 ? (long)iabs(ddx) : (long)iabs(ddy);
        long across = dx != 0 ? (long)ddy : (long)ddx;
        long score = (long)ddx * ddx + (long)ddy * ddy + 3 * across * across;
        if (along < across) continue;      /* keep the direction dominant */
        if (best < 0 || score < best_score) {
            best = i;
            best_score = score;
        }
    }
    if (best >= 0) sel = best;
}

static void start_new_chronicle(void) {
    ck3_init();
    started = 1;
    screen = SCREEN_GAME;
}

void ck3_handle_key(char key) {
    if (screen == SCREEN_HELP) {
        screen = SCREEN_MENU;
        return;
    }
    if (screen == SCREEN_MENU) {
        if (key == 'w' || key == 'W' || key == (char)0x80) {
            menu_sel = (menu_sel + 2) % 3;
        } else if (key == 's' || key == 'S' || key == (char)0x81) {
            menu_sel = (menu_sel + 1) % 3;
        } else if (key == '1') {
            start_new_chronicle();
        } else if (key == '2') {
            if (started) screen = SCREEN_GAME;
        } else if (key == '3') {
            screen = SCREEN_HELP;
        } else if (key == 10 || key == 13 || key == ' ' || key == 'e' || key == 'E') {
            if (menu_sel == 0) start_new_chronicle();
            else if (menu_sel == 1 && started) screen = SCREEN_GAME;
            else if (menu_sel == 2) screen = SCREEN_HELP;
        }
        return;
    }

    if (key == 0x1b) {
        screen = SCREEN_MENU;
        menu_sel = 1;
        return;
    }

    if (event_open) {
        if (key == '1') { event_apply(event_kind, 0); event_open = 0; }
        else if (key == '2') { event_apply(event_kind, 1); event_open = 0; }
        return;
    }
    if (game_over) {
        if (key == 'r' || key == 'R') start_new_chronicle();
        return;
    }

    switch (key) {
        case 'w': case 'W': case (char)0x80: move_selection(0, -1); break;
        case 's': case 'S': case (char)0x81: move_selection(0, 1); break;
        case 'a': case 'A': case (char)0x82: move_selection(-1, 0); break;
        case 'd': case 'D': case (char)0x83: move_selection(1, 0); break;
        case 'j': case 'J': act_declare_war(); break;
        case 'k': case 'K': act_build(); break;
        case 'f': case 'F': act_feast(); break;
        case 'p': case 'P': act_pilgrimage(); break;
        case 'h': case 'H': act_heir(); break;
        case 'e': case 'E': case ' ': case 10: case 13: advance_month(); break;
        case 'r': case 'R': start_new_chronicle(); break;
        default: break;
    }
}

void ck3_get_size(int *w, int *h) {
    if (w) *w = 1000;
    if (h) *h = 680;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/*                                                                     */
/* One polygon routine does all of it: it projects the outline, walks   */
/* the scanlines once, fills the spans and paints the two edge pixels   */
/* of each span as the border. Land, water, islands and provinces all   */
/* go through it, so there is a single place where shapes are drawn.    */
/* ------------------------------------------------------------------ */

#define MAX_POLY 671

static void text(int x, int y, const char *s, uint32_t color) {
    graphics_draw_string((uint32_t)(x < 0 ? 0 : x), (uint32_t)(y < 0 ? 0 : y),
                         s, color);
}

static void text_center(int cx, int y, const char *s, uint32_t color) {
    text(cx - slen(s) * 4, y, s, color);
}

static int proj_x(int lon) {
    return map_ox + ((int)LON(lon) * map_scale >> 8);
}

static int proj_y(int lat) {
    return map_oy + ((int)LAT(lat) * map_scale >> 8);
}

/* Fill a projected polygon and outline it. border == 0 leaves it flat.
 * Vertices are sampled at half-pixel centres so adjacent shapes that share
 * an edge meet without a gap. */
static void poly_draw(const struct geo *pts, int n, uint32_t fill,
                      int border, uint32_t border_color) {
    if (n < 3 || n > MAX_POLY) return;
    int xs[MAX_POLY];
    int ys[MAX_POLY];
    int min_y = 0, max_y = 0;

    for (int i = 0; i < n; i++) {
        xs[i] = proj_x(pts[i].lon);
        ys[i] = proj_y(pts[i].lat);
        if (i == 0 || ys[i] < min_y) min_y = ys[i];
        if (i == 0 || ys[i] > max_y) max_y = ys[i];
    }
    if (max_y < -4 || min_y > (int)graphics_get_height() + 4) return;

    for (int y = min_y; y <= max_y; y++) {
        int hits[MAX_POLY];
        int k = 0;
        int sample = y * 2 + 1;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            int y0 = ys[i] * 2;
            int y1 = ys[j] * 2;
            if ((sample >= y0 && sample < y1) || (sample >= y1 && sample < y0)) {
                int x0 = xs[i] * 2;
                int x1 = xs[j] * 2;
                if (k < MAX_POLY)
                    hits[k++] = x0 + (sample - y0) * (x1 - x0) / (y1 - y0);
            }
        }
        for (int a = 0; a + 1 < k; a++)
            for (int b = a + 1; b < k; b++)
                if (hits[b] < hits[a]) {
                    int t = hits[a];
                    hits[a] = hits[b];
                    hits[b] = t;
                }
        for (int a = 0; a + 1 < k; a += 2) {
            int left = hits[a] / 2;
            int right = hits[a + 1] / 2;
            if (right <= left) continue;
            if (fill)
                graphics_fill_rect((uint32_t)left, (uint32_t)y,
                                   (uint32_t)(right - left), 1, fill);
            if (border > 0) {
                graphics_fill_rect((uint32_t)left, (uint32_t)y,
                                   (uint32_t)border, 1, border_color);
                graphics_fill_rect((uint32_t)(right - border), (uint32_t)y,
                                   (uint32_t)border, 1, border_color);
            }
        }
    }
}

static void draw_shapes(const struct shape *list, int count, uint32_t fill,
                        int border, uint32_t border_color) {
    for (int i = 0; i < count; i++)
        poly_draw(list[i].pts, list[i].count, fill, border, border_color);
}

static void draw_province(int index) {
    struct province *p = &provinces[index];
    int thick = index == sel ? 2 : 1;
    uint32_t edge = index == sel ? C_SEL : owner_colors[p->owner];
    for (int k = 0; k < PROV_PIECE_COUNT; k++)
        if (prov_pieces[k].prov == index)
            poly_draw(&prov_points[prov_pieces[k].start], prov_pieces[k].count,
                      owner_shade[p->owner], thick, edge);
}

/* Sea names, then the province capitals and the selected province's name.
 * Only one name is printed on the map so the geography stays readable. */
static void draw_labels(void) {
    for (int i = 0; i < (int)(sizeof(sea_labels) / sizeof(sea_labels[0])); i++) {
        int cx = proj_x(sea_labels[i].at.lon);
        int cy = proj_y(sea_labels[i].at.lat);
        text_center(cx, cy, sea_labels[i].name, 0xFF3E5E77);
    }

    for (int i = 0; i < PROVINCE_COUNT; i++) {
        int cx = proj_x(province_defs[i].label_at.lon);
        int cy = proj_y(province_defs[i].label_at.lat);
        graphics_fill_rect((uint32_t)(cx - 2), (uint32_t)(cy - 2), 5, 5,
                           0xCC0D1119);
        graphics_fill_rect((uint32_t)(cx - 1), (uint32_t)(cy - 1), 3, 3,
                           i == sel ? C_SEL : 0xFFE6EAF2);
        if (war_on && i == war_target)
            graphics_fill_rect((uint32_t)(cx - 3), (uint32_t)(cy - 3), 7, 7,
                               C_WAR);
    }

    int cx = proj_x(province_defs[sel].label_at.lon);
    int cy = proj_y(province_defs[sel].label_at.lat);
    const char *name = province_defs[sel].name;
    int plate_w = slen(name) * 8 + 12;
    int plate_x = cx - plate_w / 2;
    int plate_y = cy - 24;
    graphics_fill_rect((uint32_t)(plate_x - 1), (uint32_t)(plate_y - 1),
                       (uint32_t)(plate_w + 2), 15, 0xFF0A0F18);
    graphics_draw_rect((uint32_t)plate_x, (uint32_t)plate_y,
                       (uint32_t)plate_w, 13, C_SEL);
    text(cx - slen(name) * 4, plate_y + 3, name, C_SEL);
}

static void draw_map(int x, int y, int w, int h) {
    graphics_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, C_SEA);

    int sx = (w << 8) / MAP_W;
    int sy = (h << 8) / MAP_H;
    map_scale = sx < sy ? sx : sy;
    int used_w = (MAP_W * map_scale) >> 8;
    int used_h = (MAP_H * map_scale) >> 8;
    map_ox = x + (w - used_w) / 2;
    map_oy = y + (h - used_h) / 2;

    /* One pass per ring: filled and outlined together, so the coast is
     * crisp and nothing has to be masked out afterwards. */
    draw_shapes(lands, LAND_COUNT, C_LAND, 2, C_COAST);

    for (int i = 0; i < PROVINCE_COUNT; i++) draw_province(i);
    draw_labels();
}

static void draw_header(int x, int y, int w) {
    graphics_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, 30, C_PANEL);
    graphics_fill_rect((uint32_t)x, (uint32_t)(y + 29), (uint32_t)w, 1, C_LINE);

    text(x + 12, y + 7, "CRUSADER KINGS III", C_GOLD);
    text(x + 12 + 18 * 8, y + 7, "- a Solis chronicle", C_DIM);

    char date[32];
    scopy(date, "", 32);
    snum(date, month, 32);
    sappend(date, "/", 32);
    snum(date, year, 32);
    int dw = slen(date) * 8;
    text(x + w - dw - 12, y + 7, date, C_TEXT);
}

static void draw_stat_row(int x, int y, const char *label, int value,
                          uint32_t color) {
    text(x, y, label, C_DIM);
    char line[24];
    scopy(line, "", 24);
    snum(line, value, 24);
    text(x + 76, y, line, color);
}

static void draw_side(int x, int y, int w, int h) {
    graphics_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, C_PANEL);
    graphics_draw_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, C_LINE);

    int px = x + 10;
    int py = y + 8;

    char line[52];
    scopy(line, "HOUSE ", 52);
    sappend(line, rulers[0].house, 52);
    text(px, py, line, C_HEAD);
    py += 18;

    scopy(line, rulers[0].name, 52);
    sappend(line, ", age ", 52);
    snum(line, rulers[0].age, 52);
    text(px, py, line, C_TEXT);
    py += 24;

    draw_stat_row(px, py, "Gold", rulers[0].gold, C_GOLD); py += 17;
    draw_stat_row(px, py, "Prestige", rulers[0].prestige, C_TEXT); py += 17;
    draw_stat_row(px, py, "Piety", rulers[0].piety, C_PIETY); py += 17;
    draw_stat_row(px, py, "Levies", realm_levies(), C_OK); py += 17;
    draw_stat_row(px, py, "Provinces", realm_provinces(0), C_TEXT); py += 22;

    if (rulers[0].heir) {
        scopy(line, "Heir age ", 52);
        snum(line, rulers[0].heir_age, 52);
        text(px, py, line, C_OK);
    } else {
        text(px, py, "No heir - press H", C_WAR);
    }
    py += 24;

    graphics_fill_rect((uint32_t)px, (uint32_t)py, (uint32_t)(w - 20), 1, C_LINE);
    py += 8;

    text(px, py, "SELECTED PROVINCE", C_HEAD);
    py += 17;
    text(px, py, province_defs[sel].name, C_TEXT); py += 15;
    scopy(line, terrain_names[provinces[sel].terrain], 52);
    sappend(line, "   tax ", 52);
    snum(line, province_tax(sel), 52);
    text(px, py, line, C_DIM); py += 15;
    scopy(line, "levies ", 52);
    snum(line, provinces[sel].levy, 52);
    sappend(line, "   dev ", 52);
    snum(line, provinces[sel].dev, 52);
    text(px, py, line, C_DIM); py += 15;
    if (provinces[sel].owner == 0) {
        text(px, py, "Held by you", C_OK);
    } else {
        scopy(line, "Held by ", 52);
        sappend(line, rulers[provinces[sel].owner].house, 52);
        text(px, py, line, owner_colors[provinces[sel].owner]);
    }
    py += 22;

    if (war_on) {
        graphics_fill_rect((uint32_t)px, (uint32_t)py, (uint32_t)(w - 20), 44, C_PANEL2);
        scopy(line, "WAR vs ", 52);
        sappend(line, rulers[war_enemy].house, 52);
        text(px + 6, py + 5, line, C_WAR);
        int bar_w = w - 32;
        int score = war_score;
        if (score < 0) score = 0;
        if (score > 100) score = 100;
        graphics_fill_rect((uint32_t)(px + 6), (uint32_t)(py + 26),
                           (uint32_t)bar_w, 8, C_LINE);
        graphics_fill_rect((uint32_t)(px + 6), (uint32_t)(py + 26),
                           (uint32_t)(bar_w * score / 100), 8, C_OK);
        py += 50;
    } else {
        py += 2;
    }

    /* Chronicle */
    text(px, py, "CHRONICLE", C_HEAD);
    py += 15;
    int lines = (y + h - 8 - py) / 15;
    if (lines > chron_count) lines = chron_count;
    if (lines > 6) lines = 6;
    for (int i = 0; i < lines; i++) {
        int idx = chron_count - lines + i;
        char clipped[32];
        if (idx < 0) idx = 0;
        scopy(clipped, chron[idx], 32);
        if (slen(clipped) > 28) clipped[28] = '\0';
        text(px, py, clipped, i == lines - 1 ? C_TEXT : C_DIM);
        py += 15;
    }
}

static void draw_event(int x, int y, int w, int h) {
    const struct event_def *e = &events[event_kind];
    int bw = 440;
    int bh = 168;
    if (bw > w - 20) bw = w - 20;
    int bx = x + (w - bw) / 2;
    int by = y + (h - bh) / 2;

    graphics_fill_rect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)bh,
                       0xFF101420);
    graphics_draw_rect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)bh, C_SEL);
    graphics_fill_rect((uint32_t)(bx + 1), (uint32_t)(by + 1),
                       (uint32_t)(bw - 2), 26, C_PANEL2);

    text(bx + 10, by + 6, e->title, C_SEL);
    text(bx + 10, by + 40, e->text1, C_TEXT);
    text(bx + 10, by + 56, e->text2, C_DIM);
    text(bx + 10, by + 88, "[1]", C_OK);
    text(bx + 40, by + 88, e->opt1, C_TEXT);
    text(bx + 10, by + 112, "[2]", C_WAR);
    text(bx + 40, by + 112, e->opt2, C_TEXT);
    text(bx + 10, by + 140, "Choose one to continue the month.", C_DIM);
}

static void draw_menu(int x, int y, int w, int h) {
    draw_map(x, y, w, h);
    graphics_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h,
                       0x22060A12);

    int pw = 460;
    int ph = 268;
    if (pw > w - 40) pw = w - 40;
    int px = x + (w - pw) / 2;
    int py = y + (h - ph) / 2;

    graphics_fill_rect((uint32_t)(px + 4), (uint32_t)(py + 5),
                       (uint32_t)pw, (uint32_t)ph, 0x66070A10);
    graphics_fill_rect((uint32_t)px, (uint32_t)py, (uint32_t)pw, (uint32_t)ph,
                       0xF00D1119);
    graphics_draw_rect((uint32_t)px, (uint32_t)py, (uint32_t)pw, (uint32_t)ph,
                       C_GOLD);
    graphics_fill_rect((uint32_t)(px + 1), (uint32_t)(py + 1),
                       (uint32_t)(pw - 2), 54, 0xF0161B26);
    graphics_fill_rect((uint32_t)(px + 1), (uint32_t)(py + 55),
                       (uint32_t)(pw - 2), 1, C_LINE);

    /* A small crown above the title, drawn from rectangles. */
    int cx = px + pw / 2;
    int cy = py + 14;
    graphics_fill_rect((uint32_t)(cx - 30), (uint32_t)(cy + 14), 60, 5, C_GOLD);
    graphics_fill_rect((uint32_t)(cx - 30), (uint32_t)cy, 6, 16, C_GOLD);
    graphics_fill_rect((uint32_t)(cx - 3), (uint32_t)(cy - 6), 6, 22, C_GOLD);
    graphics_fill_rect((uint32_t)(cx + 24), (uint32_t)cy, 6, 16, C_GOLD);
    graphics_fill_rect((uint32_t)(cx - 22), (uint32_t)(cy + 6), 8, 4, C_GOLD);
    graphics_fill_rect((uint32_t)(cx + 14), (uint32_t)(cy + 6), 8, 4, C_GOLD);

    text_center(cx, py + 40, "CRUSADER KINGS III", C_GOLD);

    const char *items[3] = {
        "New chronicle",
        "Continue chronicle",
        "How to play"
    };
    int iy = py + 78;
    for (int i = 0; i < 3; i++) {
        int disabled = (i == 1 && !started);
        uint32_t color = disabled ? 0xFF4A5262
                                  : (i == menu_sel ? C_SEL : C_TEXT);
        if (i == menu_sel) {
            graphics_fill_rect((uint32_t)(px + 60), (uint32_t)(iy - 4),
                               (uint32_t)(pw - 120), 22, 0xFF1D2536);
            graphics_draw_rect((uint32_t)(px + 60), (uint32_t)(iy - 4),
                               (uint32_t)(pw - 120), 22, C_ACTIVE_BORDER);
        }
        text_center(cx, iy, items[i], color);
        iy += 30;
    }

    text_center(cx, py + ph - 44, "1066 - the map of Europe awaits", C_DIM);
    text_center(cx, py + ph - 26, "W/S choose   Enter select   Esc returns here", C_DIM);
}

static void draw_help(int x, int y, int w, int h) {
    static const char *const lines[] = {
        "W A S D      move the selection across Europe",
        "J            declare war on the selected province",
        "K            build up the selected province (60 gold)",
        "F            hold a feast (30 gold, +prestige)",
        "P            go on pilgrimage (50 gold, +piety)",
        "H            marry and beget an heir (40 gold)",
        "E or Space   advance one month",
        "R            begin a new chronicle",
        "Esc          return to the main menu",
        "",
        "Hold every province you can: taxes pay for wars, wars win",
        "provinces, and provinces raise the levies that win wars.",
    };
    graphics_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, C_BG);

    int px = x + 40;
    int py = y + 40;
    text(px, py, "HOW TO PLAY", C_GOLD);
    py += 30;
    int n = (int)(sizeof(lines) / sizeof(lines[0]));
    for (int i = 0; i < n; i++) {
        text(px, py, lines[i], i == 7 || i == 8 ? C_TEXT : C_DIM);
        py += 18;
    }
    text(px, py + 16, "Press any key to return to the menu.", C_SEL);
}

void ck3_draw(int x, int y, int w, int h) {
    if (w < 240 || h < 180) return;

    if (screen == SCREEN_HELP) {
        draw_help(x, y, w, h);
        return;
    }
    if (screen == SCREEN_MENU) {
        draw_menu(x, y, w, h);
        return;
    }

    graphics_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, C_BG);
    draw_header(x, y, w);

    int top = y + 34;
    int body_h = h - 34 - 22;
    int map_w = w * 74 / 100;
    int side_w = w - map_w - 16;

    draw_map(x + 8, top, map_w - 8, body_h);
    draw_side(x + map_w + 8, top, side_w, body_h);

    /* Footer controls */
    int fy = y + h - 18;
    graphics_fill_rect((uint32_t)x, (uint32_t)(fy - 4), (uint32_t)w, 1, C_LINE);
    text(x + 10, fy,
         "WASD move  J war  K build  F feast  P pilgrim  H heir  E month  R restart  Esc menu",
         C_DIM);

    if (event_open) draw_event(x, top, w, body_h);

    if (game_over) {
        int bw = 400, bh = 130;
        int bx = x + (w - bw) / 2;
        int by = y + (h - bh) / 2;
        graphics_fill_rect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)bh,
                           0xFF0E1018);
        graphics_draw_rect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)bh, C_WAR);
        text(bx + 20, by + 16, "THE DYNASTY ENDS", C_WAR);
        text(bx + 20, by + 44, "No heir survives to hold your seat.", C_TEXT);
        char line[40];
        scopy(line, "Final prestige: ", 40);
        snum(line, rulers[0].prestige, 40);
        text(bx + 20, by + 68, line, C_DIM);
        text(bx + 20, by + 96, "Press R to begin a new chronicle.", C_SEL);
    }
}

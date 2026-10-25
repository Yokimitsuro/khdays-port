/* The languages and the port's own text (ui_text.h). */
#include "ui_text.h"

#include <SDL3/SDL.h>

static const char *const codes[] = {"system", "en", "fr", "de", "it", "es"};
static const char *const names[] = {NULL, "English", "Français", "Deutsch", "Italiano", "Español"};

int khdays_language_from_code(const char *code)
{
    if (code == NULL) {
        return 0;
    }
    for (int l = KHDAYS_LANGUAGE_ENGLISH; l <= KHDAYS_LANGUAGE_LAST; ++l) {
        if (SDL_strncasecmp(code, codes[l], 2) == 0) {
            return l;
        }
    }
    return 0;
}

const char *khdays_language_code(int language)
{
    return language >= 0 && language <= KHDAYS_LANGUAGE_LAST ? codes[language] : codes[0];
}

const char *khdays_language_name(int language)
{
    if (language >= KHDAYS_LANGUAGE_ENGLISH && language <= KHDAYS_LANGUAGE_LAST) {
        return names[language];
    }
    return khdays_text(TXT_SYSTEM);
}

int khdays_language_of_system(void)
{
    int count = 0, found = 0;
    SDL_Locale **locales = SDL_GetPreferredLocales(&count);
    for (int i = 0; locales != NULL && i < count && found == 0; ++i) {
        found = khdays_language_from_code(locales[i]->language);
    }
    SDL_free(locales);
    return found != 0 ? found : KHDAYS_LANGUAGE_ENGLISH; /* English, as the port defaults */
}

int khdays_language_choose(int setting)
{
    const int forced = khdays_language_from_code(SDL_getenv("KHDAYS_LANGUAGE"));
    if (forced != 0) {
        return forced;
    }
    if (setting >= KHDAYS_LANGUAGE_ENGLISH && setting <= KHDAYS_LANGUAGE_LAST) {
        return setting;
    }
    return khdays_language_of_system();
}

/* ---- The text ------------------------------------------------------------ */

/* Each text in English, French, German, Italian and Spanish. The words for the game's own Config page
 * (Config, Controls, Type A, Command List, Only) are the game's, in each language. */
static const char *const texts[TXT_COUNT][5] = {
    [TXT_SETTINGS] = {"Settings", "Réglages", "Einstellungen", "Impostazioni", "Ajustes"},
    [TXT_CONTROLS] = {"Controls", "Commandes", "Steuerung", "Comandi", "Controles"},
    [TXT_GAMEPAD_PRESET] = {"Gamepad preset", "Disposition manette", "Controller-Belegung", "Schema controller",
                            "Esquema del mando"},
    [TXT_KEYBOARD] = {"Keyboard", "Clavier", "Tastatur", "Tastiera", "Teclado"},
    [TXT_GAMEPAD] = {"Gamepad", "Manette", "Controller", "Controller", "Mando"},
    [TXT_ENHANCEMENTS] = {"Enhancements", "Améliorations", "Verbesserungen", "Miglioramenti", "Mejoras"},
    [TXT_STICKS_AND_CAMERA] = {"Sticks and camera", "Sticks et caméra", "Sticks und Kamera", "Levette e telecamera",
                               "Palancas y cámara"},
    [TXT_LANGUAGE] = {"Language", "Langue", "Sprache", "Lingua", "Idioma"},
    /* a scene keeps the language it was built in */
    [TXT_LANGUAGE_NOTE] = {"This menu changes at once; the game's own text when the screen you are on ends (a "
                           "mission, a menu of the game, the title).",
                           "Ce menu change aussitôt ; le texte du jeu, quand l'écran en cours se termine (une "
                           "mission, un menu du jeu, l'écran titre).",
                           "Dieses Menü ändert sich sofort, der Text des Spiels, wenn der aktuelle Bildschirm "
                           "endet (eine Mission, ein Menü des Spiels, der Titel).",
                           "Questo menu cambia subito; il testo del gioco, quando finisce la schermata in corso "
                           "(una missione, un menu del gioco, il titolo).",
                           "Este menú cambia al momento; el texto del juego, cuando termina la pantalla en la que "
                           "estás (una misión, un menú del juego, el título)."},
    [TXT_SESSION_LOG] = {"Session log", "Journal de session", "Sitzungsprotokoll", "Registro di sessione",
                         "Registro de sesión"},
    [TXT_SESSION_LOG_NOTE] = {"Writes what the session does to saves/logs, to send when something seems wrong; F12 "
                              "marks the moment.",
                              "Écrit ce que fait la session dans saves/logs, à envoyer si quelque chose cloche ; F12 "
                              "marque le moment.",
                              "Schreibt den Ablauf der Sitzung nach saves/logs, zum Einsenden, wenn etwas nicht "
                              "stimmt; F12 markiert den Moment.",
                              "Scrive ciò che fa la sessione in saves/logs, da inviare se qualcosa non va; F12 "
                              "segna il momento.",
                              "Escribe lo que hace la sesión en saves/logs, para enviarlo si algo va mal; F12 marca "
                              "el momento."},
    [TXT_LANGUAGE_FIXED_NOTE] = {"KHDAYS_LANGUAGE is set, and it wins over this setting.",
                                 "KHDAYS_LANGUAGE est défini : il l'emporte sur ce réglage.",
                                 "KHDAYS_LANGUAGE ist gesetzt und hat Vorrang vor dieser Einstellung.",
                                 "KHDAYS_LANGUAGE è impostato e prevale su questa impostazione.",
                                 "KHDAYS_LANGUAGE está definida y manda sobre este ajuste."},
    [TXT_SYSTEM] = {"System", "Système", "System", "Sistema", "Sistema"},
    [TXT_CLOSE] = {"Close", "Fermer", "Schließen", "Chiudi", "Cerrar"},
    [TXT_BACK] = {"Back", "Retour", "Zurück", "Indietro", "Volver"},
    [TXT_NO_GAMEPAD] = {"No gamepad connected", "Aucune manette connectée", "Kein Controller verbunden",
                        "Nessun controller collegato", "Ningún mando conectado"},
    [TXT_NOTHING] = {"Nothing", "Rien", "Nichts", "Niente", "Nada"},
    [TXT_DPAD_MOVE] = {"D-pad (move)", "Croix (déplacement)", "Steuerkreuz (Bewegen)", "Croce (movimento)",
                       "Cruceta (moverse)"},
    [TXT_CAMERA] = {"Camera", "Caméra", "Kamera", "Telecamera", "Cámara"},
    [TXT_NORMAL] = {"Normal", "Normal", "Normal", "Normale", "Normal"},
    [TXT_INVERTED] = {"Inverted", "Inversé", "Invertiert", "Invertito", "Invertido"},
    [TXT_ON] = {"On", "Oui", "An", "Sì", "Sí"},
    [TXT_OFF] = {"Off", "Non", "Aus", "No", "No"},
    [TXT_NOT_AVAILABLE] = {"Not available yet.", "Pas encore disponible.", "Noch nicht verfügbar.",
                           "Non ancora disponibile.", "Aún no disponible."},
    [TXT_MODIFIED] = {" (modified)", " (modifié)", " (geändert)", " (modificato)", " (modificado)"},
    [TXT_ACT_UP] = {"D-pad Up", "Croix haut", "Steuerkreuz oben", "Croce su", "Cruceta arriba"},
    [TXT_ACT_DOWN] = {"D-pad Down", "Croix bas", "Steuerkreuz unten", "Croce giù", "Cruceta abajo"},
    [TXT_ACT_LEFT] = {"D-pad Left", "Croix gauche", "Steuerkreuz links", "Croce sinistra", "Cruceta izquierda"},
    [TXT_ACT_RIGHT] = {"D-pad Right", "Croix droite", "Steuerkreuz rechts", "Croce destra", "Cruceta derecha"},
    [TXT_ACT_LOCK_ON] = {"Lock on", "Lock-on", "Erfassen", "Punta", "Fijar objetivo"},
    [TXT_ACT_TARGET_NEXT] = {"Next target", "Cible suivante", "Nächstes Ziel", "Bersaglio successivo",
                             "Objetivo siguiente"},
    [TXT_ACT_TARGET_PREV] = {"Previous target", "Cible précédente", "Vorheriges Ziel", "Bersaglio precedente",
                             "Objetivo anterior"},
    [TXT_ACT_MENU_UP] = {"Command menu up", "Commandes : haut", "Befehlsmenü hoch", "Menu comandi su",
                         "Menú de comandos arriba"},
    [TXT_ACT_MENU_DOWN] = {"Command menu down", "Commandes : bas", "Befehlsmenü runter", "Menu comandi giù",
                           "Menú de comandos abajo"},
    [TXT_ACT_MENU_LEFT] = {"Command menu left", "Commandes : gauche", "Befehlsmenü links", "Menu comandi sinistra",
                           "Menú de comandos izquierda"},
    [TXT_ACT_MENU_RIGHT] = {"Command menu right", "Commandes : droite", "Befehlsmenü rechts", "Menu comandi destra",
                            "Menú de comandos derecha"},
    [TXT_ACT_OPEN_SETTINGS] = {"Open settings", "Ouvrir les réglages", "Einstellungen öffnen", "Apri impostazioni",
                               "Abrir ajustes"},
    [TXT_ACT_HUD_MODE] = {"HUD mode (one screen)", "Mode du HUD (un écran)", "HUD-Modus (ein Bildschirm)",
                          "Modalità HUD (uno schermo)", "Modo del HUD (una pantalla)"},
    [TXT_PAD_SOUTH] = {"Bottom button", "Bouton bas", "Taste unten", "Tasto in basso", "Botón inferior"},
    [TXT_PAD_EAST] = {"Right button", "Bouton droit", "Taste rechts", "Tasto destro", "Botón derecho"},
    [TXT_PAD_WEST] = {"Left button", "Bouton gauche", "Taste links", "Tasto sinistro", "Botón izquierdo"},
    [TXT_PAD_NORTH] = {"Top button", "Bouton haut", "Taste oben", "Tasto in alto", "Botón superior"},
    [TXT_PAD_DPUP] = {"D-pad up", "Croix haut", "Steuerkreuz oben", "Croce su", "Cruceta arriba"},
    [TXT_PAD_DPDOWN] = {"D-pad down", "Croix bas", "Steuerkreuz unten", "Croce giù", "Cruceta abajo"},
    [TXT_PAD_DPLEFT] = {"D-pad left", "Croix gauche", "Steuerkreuz links", "Croce sinistra", "Cruceta izquierda"},
    [TXT_PAD_DPRIGHT] = {"D-pad right", "Croix droite", "Steuerkreuz rechts", "Croce destra", "Cruceta derecha"},
    [TXT_PAD_CAPTURE] = {"Capture", "Capture", "Aufnahme", "Cattura", "Captura"},
    [TXT_PAD_TOUCHPAD] = {"Touch pad", "Pavé tactile", "Touchpad", "Touchpad", "Panel táctil"},
    [TXT_PAD_PADDLE] = {"Paddle %d", "Palette %d", "Paddle %d", "Paletta %d", "Paleta %d"},
    [TXT_LABEL_CROSS] = {"Cross", "Croix", "Kreuz", "Croce", "Cruz"},
    [TXT_LABEL_CIRCLE] = {"Circle", "Rond", "Kreis", "Cerchio", "Círculo"},
    [TXT_LABEL_SQUARE] = {"Square", "Carré", "Quadrat", "Quadrato", "Cuadrado"},
    [TXT_LABEL_TRIANGLE] = {"Triangle", "Triangle", "Dreieck", "Triangolo", "Triángulo"},
    [TXT_POS_BOTTOM] = {"bottom", "bas", "unten", "in basso", "abajo"},
    [TXT_POS_RIGHT] = {"right", "droite", "rechts", "a destra", "derecha"},
    [TXT_POS_LEFT] = {"left", "gauche", "links", "a sinistra", "izquierda"},
    [TXT_POS_TOP] = {"top", "haut", "oben", "in alto", "arriba"},
    [TXT_HEADING_LOCK_ON] = {"Separate lock-on", "Lock-on séparé", "Separates Erfassen", "Puntamento separato",
                             "Fijado separado"},
    [TXT_HEADING_COMMAND_MENU] = {"Command menu (in a field; elsewhere the D-pad)",
                                  "Commandes (sur le terrain ; ailleurs, la croix)",
                                  "Befehlsmenü (im Feld; sonst Steuerkreuz)",
                                  "Menu comandi (sul campo; altrove la croce)",
                                  "Menú de comandos (en el campo; fuera, cruceta)"},
    [TXT_PRESET_DS] = {"Use the DS layout (the default)", "Disposition de la DS (par défaut)", "DS-Belegung (Standard)",
                       "Schema del DS (predefinito)", "Esquema de la DS (por defecto)"},
    [TXT_PRESET_KH2] = {"Use the Kingdom Hearts II layout", "Disposition de Kingdom Hearts II",
                        "Kingdom Hearts II-Belegung", "Schema di Kingdom Hearts II", "Esquema de Kingdom Hearts II"},
    [TXT_KH2_TITLE] = {"The Kingdom Hearts II layout:", "La disposition de Kingdom Hearts II :",
                       "Die Kingdom Hearts II-Belegung:", "Lo schema di Kingdom Hearts II:",
                       "El esquema de Kingdom Hearts II:"},
    [TXT_KH2_LINE1] = {"  Bottom button: attack. Right: jump.", "  Bouton bas : attaque. Droit : saut.",
                       "  Unten: Angriff. Rechts: Sprung.", "  In basso: attacco. Destro: salto.",
                       "  Inferior: atacar. Derecho: saltar."},
    [TXT_KH2_LINE2] = {"  Left: guard. Top: DS X.", "  Gauche : garde. Haut : X de la DS.",
                       "  Links: Abwehr. Oben: DS-X.", "  Sinistro: parata. In alto: X del DS.",
                       "  Izquierdo: bloquear. Superior: X de DS."},
    [TXT_KH2_LINE3] = {"  L1 + a button: shortcuts.", "  L1 + un bouton : raccourcis.", "  L1 + Taste: Kurzbefehle.",
                       "  L1 + un tasto: scorciatoie.", "  L1 + un botón: atajos."},
    [TXT_KH2_LINE4] = {"  R1: lock on. R2: next target.", "  R1 : lock-on. R2 : cible suivante.",
                       "  R1: erfassen. R2: nächstes Ziel.", "  R1: punta. R2: bersaglio dopo.",
                       "  R1: fijar. R2: siguiente objetivo."},
    [TXT_KH2_LINE5] = {"  R3: DS R (resets the camera).", "  R3 : R de la DS (recentre la caméra).",
                       "  R3: DS-R (setzt die Kamera zurück).", "  R3: R del DS (reimposta la telecamera).",
                       "  R3: R de DS (recoloca la cámara)."},
    [TXT_KH2_LINE6] = {"  View / touch pad: DS Select.", "  View / pavé tactile : Select de la DS.",
                       "  View / Touchpad: DS-Select.", "  View / touchpad: Select del DS.",
                       "  View / panel táctil: Select de DS."},
    [TXT_KH2_LINE7] = {"  D-pad: the command menu.", "  Croix : le menu de commandes.", "  Steuerkreuz: Befehlsmenü.",
                       "  Croce: menu comandi.", "  Cruceta: menú de comandos."},
    [TXT_KH2_LINE8] = {"  Right stick: the camera.", "  Stick droit : la caméra.", "  Rechter Stick: Kamera.",
                       "  Levetta destra: telecamera.", "  Palanca derecha: cámara."},
    /* %s: the game's words, "Config", "Controls" and "Type A". */
    [TXT_KH2_SHORTCUTS1] = {"Shortcuts on L1 need the game's", "Les raccourcis sur L1 demandent",
                            "Kurzbefehle auf L1 brauchen", "Le scorciatoie su L1 richiedono",
                            "Los atajos en L1 necesitan"},
    [TXT_KH2_SHORTCUTS2] = {"%s > %s: %s (as in a new game).", "%s > %s : %s (nouvelle partie).",
                            "%s > %s: %s (wie im neuen Spiel).", "%s > %s: %s (nuova partita).",
                            "%s > %s: %s (como en partida nueva)."},
    [TXT_KEYBOARD_UNCHANGED] = {"The keyboard is not changed.", "Le clavier ne change pas.",
                                "Die Tastatur bleibt unverändert.", "La tastiera non cambia.", "El teclado no cambia."},
    [TXT_ENH_CAMERA] = {"Analog camera", "Caméra analogique", "Analoge Kamera", "Telecamera analogica",
                        "Cámara analógica"},
    [TXT_ENH_CAMERA_NOTE] = {"The right stick turns the camera, as the game's own camera controls do.",
                             "Le stick droit tourne la caméra, comme les commandes de caméra du jeu.",
                             "Der rechte Stick dreht die Kamera wie die Kamerasteuerung des Spiels.",
                             "La levetta destra gira la telecamera come i comandi del gioco.",
                             "La palanca derecha gira la cámara como los controles de cámara del juego."},
    [TXT_ENH_LOCK_ON] = {"Separate lock-on", "Lock-on séparé", "Separates Erfassen", "Puntamento separato",
                         "Fijado separado"},
    [TXT_ENH_LOCK_ON_NOTE] = {"Lock on and switch target on their own buttons (the game's own lock-on: R twice).",
                              "Lock-on et changement de cible sur leurs propres boutons (le lock-on du jeu : R "
                              "deux fois).",
                              "Erfassen und Zielwechsel auf eigenen Tasten (das Erfassen des Spiels: zweimal R).",
                              "Punta e cambio bersaglio su tasti propri (la punta del gioco: R due volte).",
                              "Fijar y cambiar de objetivo con botones propios (el fijado del juego: R dos veces)."},
    [TXT_ENH_MENU] = {"Command menu on the D-pad", "Commandes sur la croix", "Befehlsmenü auf dem Steuerkreuz",
                      "Menu comandi sulla croce", "Menú de comandos en la cruceta"},
    /* %s: the game's words, "Config", "Command List" and "X Only". */
    [TXT_ENH_MENU_NOTE] = {"In a field the gamepad's D-pad moves through the commands. With %s > %s: %s, "
                           "only Down can.",
                           "Sur le terrain, la croix de la manette parcourt les commandes. Avec %s > %s : "
                           "%s, seul Bas fonctionne.",
                           "Im Feld blättert das Steuerkreuz des Controllers durch die Befehle. Mit %s > %s: "
                           "%s geht nur Unten.",
                           "Sul campo la croce del controller scorre i comandi. Con %s > %s: %s, funziona "
                           "solo Giù.",
                           "En el campo, la cruceta del mando recorre los comandos. Con %s > %s: %s, solo "
                           "funciona Abajo."},
    [TXT_ENH_MOVEMENT] = {"Analog movement", "Déplacement analogique", "Analoge Bewegung", "Movimento analogico",
                          "Movimiento analógico"},
    [TXT_ENH_MOVEMENT_NOTE] = {"Move in any direction; push gently to walk.",
                               "Déplacez-vous dans toutes les directions ; poussez doucement pour marcher.",
                               "In jede Richtung bewegen; sanft drücken, um zu gehen.",
                               "Muoviti in ogni direzione; spingi piano per camminare.",
                               "Muévete en cualquier dirección; empuja suave para andar."},
    [TXT_LEFT_STICK] = {"Left stick", "Stick gauche", "Linker Stick", "Levetta sinistra", "Palanca izquierda"},
    [TXT_RIGHT_STICK] = {"Right stick", "Stick droit", "Rechter Stick", "Levetta destra", "Palanca derecha"},
    [TXT_LEFT_DEAD_ZONE] = {"Left stick dead zone (%)", "Zone morte stick gauche (%)", "Totzone linker Stick (%)",
                            "Zona morta lev. sinistra (%)", "Zona muerta pal. izquierda (%)"},
    [TXT_RIGHT_DEAD_ZONE] = {"Right stick dead zone (%)", "Zone morte stick droit (%)", "Totzone rechter Stick (%)",
                             "Zona morta lev. destra (%)", "Zona muerta pal. derecha (%)"},
    [TXT_LEFT_HYSTERESIS] = {"Left stick hysteresis (%)", "Hystérésis stick gauche (%)", "Hysterese linker Stick (%)",
                             "Isteresi lev. sinistra (%)", "Histéresis pal. izquierda (%)"},
    [TXT_RIGHT_HYSTERESIS] = {"Right stick hysteresis (%)", "Hystérésis stick droit (%)", "Hysterese rechter Stick (%)",
                              "Isteresi lev. destra (%)", "Histéresis pal. derecha (%)"},
    [TXT_CAMERA_SPEED] = {"Camera speed (%)", "Vitesse de la caméra (%)", "Kameratempo (%)", "Velocità telecamera (%)",
                          "Velocidad de la cámara (%)"},
    [TXT_CAMERA_X] = {"Camera left/right", "Caméra gauche/droite", "Kamera links/rechts", "Telecamera sinistra/destra",
                      "Cámara izquierda/derecha"},
    [TXT_CAMERA_Y] = {"Camera up/down", "Caméra haut/bas", "Kamera hoch/runter", "Telecamera su/giù",
                      "Cámara arriba/abajo"},
    [TXT_RUN_FROM] = {"Run from (% of the stick)", "Courir dès (% du stick)", "Laufen ab (% des Sticks)",
                      "Corsa da (% della levetta)", "Correr desde (% de palanca)"},
    [TXT_WALK_SPEED] = {"Walking speed (%)", "Vitesse de marche (%)", "Gehtempo (%)", "Velocità camminata (%)",
                        "Velocidad al andar (%)"},
    [TXT_RESTORE_KEYBOARD] = {"Restore the keyboard's defaults", "Clavier par défaut", "Tastatur zurücksetzen",
                              "Ripristina la tastiera", "Restablecer el teclado"},
    [TXT_RESTORE_GAMEPAD] = {"Restore the gamepad's defaults", "Manette par défaut", "Controller zurücksetzen",
                             "Ripristina il controller", "Restablecer el mando"},
    [TXT_RESTORE_BOTH] = {"Restore everything", "Tout rétablir", "Alles zurücksetzen", "Ripristina tutto",
                          "Restablecer todo"},
    /* the buttons the player's DS A, Y and B are on (settings_menu.cpp face_buttons) */
    [TXT_HINTS_GAMEPAD] = {"D-pad: choose   %s: change   %s: clear   %s: back",
                           "Croix : choisir   %s : modifier   %s : effacer   %s : retour",
                           "Steuerkreuz: wählen   %s: ändern   %s: löschen   %s: zurück",
                           "Croce: scegli   %s: cambia   %s: cancella   %s: indietro",
                           "Cruceta: elegir   %s: cambiar   %s: borrar   %s: volver"},
    [TXT_HINTS_MOUSE] = {"Click: change   Right click: back   Wheel: scroll",
                         "Clic : modifier   Clic droit : retour   Molette : défiler",
                         "Klick: ändern   Rechtsklick: zurück   Mausrad: blättern",
                         "Clic: cambia   Clic destro: indietro   Rotellina: scorri",
                         "Clic: cambiar   Clic derecho: volver   Rueda: desplazar"},
    [TXT_HINTS_KEYBOARD] = {"Arrows: choose   Enter: change   Delete: clear   Esc: back",
                            "Flèches : choisir   Entrée : modifier   Suppr : effacer   Échap : retour",
                            "Pfeile: wählen   Enter: ändern   Entf: löschen   Esc: zurück",
                            "Frecce: scegli   Invio: cambia   Canc: cancella   Esc: indietro",
                            "Flechas: elegir   Intro: cambiar   Supr: borrar   Esc: volver"},
    [TXT_PRESS_KEY] = {"Press a key", "Appuyez sur une touche", "Drücke eine Taste", "Premi un tasto",
                       "Pulsa una tecla"},
    [TXT_PRESS_BUTTON] = {"Press a gamepad button", "Appuyez sur un bouton de la manette",
                          "Drücke eine Controller-Taste", "Premi un tasto del controller", "Pulsa un botón del mando"},
    /* %s: "Press a key" or "Press a gamepad button"; %s: what it is for. */
    [TXT_PRESS_FOR] = {"%s for \"%s\"", "%s pour \"%s\"", "%s für \"%s\"", "%s per \"%s\"", "%s para \"%s\""},
    /* %s: how to cancel; %d: the seconds left. */
    [TXT_TO_CANCEL] = {"%s to cancel (%d s).", "%s pour annuler (%d s).", "%s zum Abbrechen (%d s).",
                       "%s per annullare (%d s).", "%s para cancelar (%d s)."},
    [TXT_ESC] = {"Esc", "Échap", "Esc", "Esc", "Esc"},
    [TXT_ESC_OR_BUTTON] = {"Esc or the open-settings button", "Échap ou le bouton des réglages",
                           "Esc oder die Einstellungstaste", "Esc o il tasto delle impostazioni",
                           "Esc o el botón de ajustes"},
    /* %s: the input; %s: what it does now; %s: what it would do. */
    [TXT_ALREADY_DOES] = {"%s already does \"%s\". Use it for \"%s\" instead?",
                          "%s fait déjà \"%s\". L'utiliser plutôt pour \"%s\" ?",
                          "%s macht schon \"%s\". Stattdessen für \"%s\" nutzen?",
                          "%s fa già \"%s\". Usarlo invece per \"%s\"?", "%s ya hace \"%s\". ¿Usarlo para \"%s\"?"},
    [TXT_MOVE_IT_HERE] = {"Move it here", "Le déplacer ici", "Verschieben", "Spostalo qui", "Moverlo aquí"},
    [TXT_CANCEL] = {"Cancel", "Annuler", "Abbrechen", "Annulla", "Cancelar"},
    /* %s: the layout's name. */
    [TXT_USE_LAYOUT] = {"Use the %s layout on the gamepad?", "Utiliser la disposition %s sur la manette ?",
                        "Die %s-Belegung für den Controller nutzen?", "Usare lo schema %s sul controller?",
                        "¿Usar el esquema %s en el mando?"},
    [TXT_UNTIL_AVAILABLE] = {"Until this build has every enhancement it uses:",
                             "Tant que toutes ses améliorations ne sont pas prêtes :",
                             "Solange nicht alle Verbesserungen verfügbar sind:",
                             "Finché non ci sono tutti i miglioramenti:", "Mientras no estén todas sus mejoras:"},
    [TXT_FALLBACK_LOCK_ON] = {"R1 is DS R; R2 does nothing (no separate lock-on yet).",
                              "R1 est le R de la DS ; R2 ne fait rien (pas encore de lock-on séparé).",
                              "R1 ist DS-R; R2 macht nichts (noch kein separates Erfassen).",
                              "R1 è l'R del DS; R2 non fa nulla (niente puntamento separato per ora).",
                              "R1 es la R de DS; R2 no hace nada (aún sin fijado separado)."},
    [TXT_FALLBACK_MENU] = {"The D-pad stays the D-pad (no command menu on it yet).",
                           "La croix reste la croix (pas encore de menu de commandes).",
                           "Das Steuerkreuz bleibt Steuerkreuz (noch kein Befehlsmenü).",
                           "La croce resta la croce (niente menu comandi per ora).",
                           "La cruceta sigue siendo la cruceta (aún sin menú de comandos)."},
    [TXT_FALLBACK_CAMERA] = {"The right stick does nothing (no analog camera yet).",
                             "Le stick droit ne fait rien (pas encore de caméra analogique).",
                             "Der rechte Stick macht nichts (noch keine analoge Kamera).",
                             "La levetta destra non fa nulla (niente telecamera analogica per ora).",
                             "La palanca derecha no hace nada (aún sin cámara analógica)."},
    [TXT_FALLBACK_MOVEMENT] = {"The left stick has 8 directions (no analog movement yet).",
                               "Le stick gauche a 8 directions (pas encore de déplacement analogique).",
                               "Der linke Stick hat 8 Richtungen (noch keine analoge Bewegung).",
                               "La levetta sinistra ha 8 direzioni (niente movimento analogico per ora).",
                               "La palanca izquierda tiene 8 direcciones (aún sin movimiento analógico)."},
    [TXT_APPLY] = {"Apply", "Appliquer", "Übernehmen", "Applica", "Aplicar"},
    /* %s: the DS buttons no input would press. */
    [TXT_NO_INPUT_PRESSES] = {"No key or button would press %s.", "Aucune touche ni bouton n'appuierait sur %s.",
                              "Keine Taste würde %s drücken.", "Nessun tasto premerebbe %s.",
                              "Ninguna tecla ni botón pulsaría %s."},
    [TXT_CLOSE_ANYWAY_Q] = {"Close anyway?", "Fermer quand même ?", "Trotzdem schließen?", "Chiudere comunque?",
                            "¿Cerrar de todos modos?"},
    [TXT_CLOSE_ANYWAY] = {"Close anyway", "Fermer quand même", "Trotzdem schließen", "Chiudi comunque",
                          "Cerrar igualmente"},
    [TXT_QUIT_GAME] = {"Quit the game", "Quitter le jeu", "Spiel beenden", "Esci dal gioco", "Salir del juego"},
    [TXT_QUIT_GAME_Q] = {"Quit the game?", "Quitter le jeu ?", "Spiel beenden?", "Uscire dal gioco?",
                         "¿Salir del juego?"},
    [TXT_QUIT_GAME_DETAIL] = {"What has not been saved in the game is lost. The settings changed are kept.",
                              "Ce qui n'a pas été sauvegardé dans le jeu est perdu. Les réglages modifiés sont gardés.",
                              "Was im Spiel nicht gespeichert wurde, geht verloren. Geänderte Einstellungen bleiben.",
                              "Ciò che non è stato salvato nel gioco va perso. Le impostazioni cambiate restano.",
                              "Lo que no hayas guardado en la partida se pierde. Los ajustes cambiados se conservan."},
    /* %s: the port's version (the VERSION file's). */
    [TXT_VERSION] = {"Version %s", "Version %s", "Version %s", "Versione %s", "Versión %s"},
    /* %s: the controller's name. */
    [TXT_NOT_A_GAMEPAD] = {"%s is not recognized as a standard gamepad, so it is not used",
                           "%s n'est pas reconnue comme une manette standard : elle n'est pas utilisée",
                           "%s wird nicht als Standard-Controller erkannt und daher nicht verwendet",
                           "%s non è riconosciuto come controller standard, quindi non viene usato",
                           "%s no se reconoce como un mando estándar, así que no se usa"},
    [TXT_A_CONTROLLER] = {"A controller", "Une manette", "Ein Controller", "Un controller", "Un mando"},
    /* %s: why. */
    [TXT_GAMEPAD_NOT_OPENED] = {"a gamepad could not be opened: %s", "une manette n'a pas pu être ouverte : %s",
                                "ein Controller konnte nicht geöffnet werden: %s",
                                "non è stato possibile aprire un controller: %s", "no se pudo abrir un mando: %s"},
    /* %s: the renderer used instead (SDL's name for it). */
    [TXT_NEEDS_GRAPHICS_CARD] = {"The port needs a graphics card it can use through Direct3D 12 or Vulkan, and "
                                 "could not use one: %s",
                                 "Le portage a besoin d'une carte graphique utilisable avec Direct3D 12 ou "
                                 "Vulkan, et n'a pas pu en utiliser une : %s",
                                 "Der Port braucht eine Grafikkarte, die er über Direct3D 12 oder Vulkan "
                                 "verwenden kann, und konnte keine verwenden: %s",
                                 "Il port ha bisogno di una scheda grafica utilizzabile con Direct3D 12 o "
                                 "Vulkan, e non ne ha potuta usare nessuna: %s",
                                 "El port necesita una tarjeta gráfica que pueda usar con Direct3D 12 o "
                                 "Vulkan, y no ha podido usar ninguna: %s"},
    /* The PC would not give the game more memory (runtime/core/memory.c). */
    [TXT_OUT_OF_MEMORY] = {"Kingdom Hearts 358/2 Days has run out of memory: the PC could not give it more. "
                           "Close other programs and try again.",
                           "Kingdom Hearts 358/2 Days n'a plus de mémoire : le PC n'a pas pu lui en donner "
                           "davantage. Fermez d'autres programmes et réessayez.",
                           "Kingdom Hearts 358/2 Days hat keinen Speicher mehr: Der PC konnte ihm keinen weiteren "
                           "geben. Schließe andere Programme und versuche es erneut.",
                           "Kingdom Hearts 358/2 Days ha esaurito la memoria: il PC non ha potuto dargliene "
                           "altra. Chiudi altri programmi e riprova.",
                           "Kingdom Hearts 358/2 Days se ha quedado sin memoria: el PC no ha podido darle más. "
                           "Cierra otros programas y vuelve a intentarlo."},
    /* The Video page. */
    [TXT_VIDEO] = {"Video", "Vidéo", "Video", "Video", "Vídeo"},
    [TXT_VIDEO_RESOLUTION] = {"Internal resolution", "Résolution interne", "Interne Auflösung", "Risoluzione interna",
                              "Resolución interna"},
    [TXT_VIDEO_RESOLUTION_NOTE] = {"The 3D is drawn at this many times the DS's pixels; menus and text keep theirs.",
                                   "La 3D est dessinée à ce multiple des pixels de la DS ; menus et textes gardent "
                                   "les leurs.",
                                   "Das 3D wird mit so vielen DS-Pixeln gezeichnet; Menüs und Text behalten ihre.",
                                   "Il 3D viene disegnato a questo multiplo dei pixel del DS; menu e testi tengono i "
                                   "loro.",
                                   "El 3D se dibuja a este múltiplo de los píxeles de la DS; menús y textos "
                                   "conservan los suyos."},
    [TXT_VIDEO_RESOLUTION_WINDOW] = {"As the window allows", "Selon la fenêtre", "Wie das Fenster erlaubt",
                                     "Quanto permette la finestra", "Lo que permita la ventana"},
    [TXT_VIDEO_ANTIALIASING] = {"Antialiasing", "Anticrénelage", "Kantenglättung", "Antialiasing", "Antialiasing"},
    [TXT_VIDEO_ANTIALIASING_NOTE] = {"The picture is made this many times larger and shrunk: the 3D's edges come out "
                                     "smooth. 2x is four times the work for the graphics card, 4x sixteen.",
                                     "L'image est faite autant de fois plus grande puis réduite : les bords de la 3D "
                                     "sont lisses. 2x demande quatre fois plus à la carte graphique, 4x seize.",
                                     "Das Bild wird so viel größer gemacht und verkleinert: die Kanten des 3D werden "
                                     "glatt. 2x ist die vierfache Arbeit für die Grafikkarte, 4x die sechzehnfache.",
                                     "L'immagine è fatta tante volte più grande e poi ridotta: i bordi del 3D escono "
                                     "lisci. 2x è quattro volte il lavoro per la scheda grafica, 4x sedici.",
                                     "La imagen se hace tantas veces más grande y se reduce: los bordes del 3D salen "
                                     "suaves. 2x es cuatro veces el trabajo de la tarjeta gráfica, 4x dieciséis."},
    [TXT_VIDEO_ANTIALIASING_WARNING] =
        {"The internal resolution already draws the 3D at the window's size or larger: antialiasing "
         "multiplies that work, so the game can slow down and use much more of the graphics card. "
         "Leave it Off unless your card has power to spare.",
         "La résolution interne dessine déjà la 3D à la taille de la fenêtre ou plus : l'anticrénelage "
         "multiplie ce travail, le jeu peut ralentir et solliciter bien plus la carte graphique. "
         "Laissez-le sur Non si votre carte n'a pas de puissance à revendre.",
         "Die interne Auflösung zeichnet das 3D schon in Fenstergröße oder größer: die Kantenglättung "
         "vervielfacht diese Arbeit, das Spiel kann langsamer werden und die Grafikkarte viel stärker "
         "belasten. Lass sie auf Aus, wenn deine Karte keine Leistung übrig hat.",
         "La risoluzione interna disegna già il 3D alla dimensione della finestra o più: l'antialiasing "
         "moltiplica quel lavoro, il gioco può rallentare e impegnare molto di più la scheda grafica. "
         "Lascialo su No se la tua scheda non ha potenza da vendere.",
         "La resolución interna ya dibuja el 3D al tamaño de la ventana o mayor: el antialiasing "
         "multiplica ese trabajo, y el juego puede ir más lento y exigir mucho más a la tarjeta gráfica. "
         "Déjalo en No salvo que tu tarjeta vaya sobrada."},
    [TXT_VIDEO_SCREENS] = {"Screens", "Écrans", "Bildschirme", "Schermi", "Pantallas"},
    [TXT_VIDEO_SCREENS_TWO] = {"Two (DS)", "Deux (DS)", "Zwei (DS)", "Due (DS)", "Dos (DS)"},
    [TXT_VIDEO_SCREENS_ONE] = {"One", "Un", "Einer", "Uno", "Una"},
    [TXT_VIDEO_SCREENS_NOTE] = {"One picture: the lower screen's pieces are brought onto it.",
                                "Une seule image : les éléments de l'écran du bas y sont placés.",
                                "Ein Bild: die Teile des unteren Bildschirms werden hineingesetzt.",
                                "Un'unica immagine: gli elementi dello schermo inferiore vi sono portati.",
                                "Una sola imagen: las piezas de la pantalla de abajo se colocan en ella."},
    [TXT_VIDEO_HUD_SIZE] = {"HUD size", "Taille du HUD", "HUD-Größe", "Dimensione HUD", "Tamaño del HUD"},
    [TXT_VIDEO_HUD_SIZE_NOTE] =
        {"One screen: how big the HUD's pieces are, 1 to 8 (4 by default); the dialogs keep "
         "their size.",
         "Un écran : la taille des éléments du HUD, de 1 à 8 (4 par défaut) ; les dialogues "
         "gardent la leur.",
         "Ein Bildschirm: wie groß die HUD-Teile sind, 1 bis 8 (Standard 4); Dialoge behalten "
         "ihre Größe.",
         "Uno schermo: quanto sono grandi i pezzi dell'HUD, da 1 a 8 (4 di base); i dialoghi "
         "restano uguali.",
         "Una pantalla: el tamaño de las piezas del HUD, de 1 a 8 (4 por defecto); los diálogos "
         "conservan el suyo."},
    [TXT_VIDEO_ASPECT] = {"Aspect ratio", "Format d'image", "Seitenverhältnis", "Proporzioni", "Proporción"},
    [TXT_VIDEO_ASPECT_NOTE] = {"The 3D view widens; the HUD keeps to the corners, the menus stay in the middle.",
                               "La vue 3D s'élargit ; l'interface reste dans les coins, les menus au milieu.",
                               "Die 3D-Ansicht wird breiter; die Anzeigen bleiben in den Ecken, Menüs in der Mitte.",
                               "La vista 3D si allarga; l'interfaccia resta negli angoli, i menu al centro.",
                               "La vista 3D se ensancha; el HUD sigue en las esquinas y los menús en el centro."},
    [TXT_VIDEO_ASPECT_WINDOW] = {"As the window", "Comme la fenêtre", "Wie das Fenster", "Come la finestra",
                                 "La de la ventana"},
    [TXT_VIDEO_SCALING] = {"Screen scaling", "Mise à l'échelle", "Bildskalierung", "Ridimensionamento", "Escalado"},
    [TXT_VIDEO_SCALING_NOTE] = {"How the screens are stretched to the window.",
                                "Comment les écrans s'étirent à la taille de la fenêtre.",
                                "Wie die Bildschirme auf das Fenster gestreckt werden.",
                                "Come gli schermi vengono adattati alla finestra.",
                                "Cómo se estiran las pantallas a la ventana."},
    [TXT_VIDEO_SHARP] = {"Sharp (DS)", "Net (DS)", "Scharf (DS)", "Nitido (DS)", "Nítido (DS)"},
    [TXT_VIDEO_SMOOTH] = {"Smooth", "Lissé", "Weich", "Morbido", "Suave"},
    [TXT_VIDEO_TEXTURES] = {"3D texture smoothing", "Lissage des textures 3D", "3D-Texturglättung", "Filtro texture 3D",
                            "Suavizado de texturas 3D"},
    [TXT_VIDEO_TEXTURES_NOTE] = {"The DS draws textures without smoothing.", "La DS dessine les textures sans lissage.",
                                 "Der DS zeichnet Texturen ohne Glättung.", "Il DS disegna le texture senza filtro.",
                                 "La DS dibuja las texturas sin suavizado."},
    [TXT_VIDEO_HD] = {"HD graphics", "Graphismes HD", "HD-Grafiken", "Grafica HD", "Gráficos HD"},
    [TXT_VIDEO_DUMP] = {"Dump graphics", "Extraire les graphismes", "Grafiken ausgeben", "Esporta grafica",
                        "Volcar gráficos"},
    [TXT_VIDEO_DUMP_NOTE] = {"Writes every texture and graphic shown to saves/dump, and the colours each 2D sheet is "
                             "drawn with, to make replacements.",
                             "Écrit chaque texture et graphisme affiché dans saves/dump, et les couleurs de chaque "
                             "planche 2D, pour créer des remplacements.",
                             "Schreibt jede gezeigte Textur und Grafik nach saves/dump, dazu die Farben jedes "
                             "2D-Bogens, um Ersatz zu erstellen.",
                             "Scrive ogni texture e grafica mostrata in saves/dump, e i colori di ogni foglio 2D, per "
                             "creare sostituzioni.",
                             "Escribe cada textura y gráfico mostrado en saves/dump, y los colores con que se dibuja "
                             "cada hoja 2D, para hacer reemplazos."},
    [TXT_VIDEO_MOVIES] = {"Movies", "Vidéos", "Filmsequenzen", "Filmati", "Vídeos"},
    [TXT_VIDEO_MOVIES_NOTE] = {"HD plays the HD collection's videos copied to data/cutscenes/cinematics (hd802.mp4 "
                               "and the rest), the other movies at full colour. The DS shows its movies in 15-bit "
                               "colour, dithered.",
                               "HD lit les vidéos de la collection HD copiées dans data/cutscenes/cinematics "
                               "(hd802.mp4 et les autres), les autres vidéos en couleurs complètes. La DS affiche "
                               "ses vidéos en couleurs 15 bits, tramées.",
                               "HD spielt die Videos der HD-Sammlung, die nach data/cutscenes/cinematics kopiert "
                               "wurden (hd802.mp4 und die übrigen), die anderen Filmsequenzen in vollen Farben. "
                               "Der DS zeigt seine Filmsequenzen in 15-Bit-Farbe, gerastert.",
                               "HD riproduce i video della raccolta HD copiati in data/cutscenes/cinematics "
                               "(hd802.mp4 e gli altri), gli altri filmati a colori pieni. Il DS mostra i filmati "
                               "a colori a 15 bit, con retinatura.",
                               "HD reproduce los vídeos de la colección HD copiados en data/cutscenes/cinematics "
                               "(hd802.mp4 y los demás), los otros vídeos a color completo. La DS muestra sus "
                               "vídeos en color de 15 bits, tramado."},
    [TXT_VIDEO_MOVIES_HD] = {"HD", "HD", "HD", "HD", "HD"},
    [TXT_VIDEO_MOVIES_PC] = {"Full colour", "Couleurs complètes", "Volle Farben", "Colori pieni", "Color completo"},
    [TXT_VIDEO_MOVIES_DS] = {"As on the DS", "Comme sur DS", "Wie auf dem DS", "Come sul DS", "Como en la DS"},
    [TXT_VIDEO_PACE] = {"Picture pace", "Rythme de l'image", "Bildrate", "Ritmo dell'immagine", "Ritmo de imagen"},
    [TXT_VIDEO_PACE_NOTE] = {"Smooth draws the 3D at the monitor's rate, up to a game frame behind; the DS shows 30 "
                             "pictures a second.",
                             "Fluide dessine la 3D à la fréquence de l'écran, jusqu'à une image du jeu de retard ; "
                             "la DS affiche 30 images par seconde.",
                             "Flüssig zeichnet das 3D mit der Bildrate des Monitors, bis zu ein Spielbild später; "
                             "der DS zeigt 30 Bilder pro Sekunde.",
                             "Fluido disegna il 3D alla frequenza dello schermo, fino a un fotogramma di gioco in "
                             "ritardo; il DS mostra 30 immagini al secondo.",
                             "Fluido dibuja la 3D al ritmo del monitor, hasta un fotograma del juego por detrás; "
                             "la DS muestra 30 imágenes por segundo."},
    [TXT_VIDEO_PACE_SMOOTH] = {"Smooth", "Fluide", "Flüssig", "Fluido", "Fluido"},
    [TXT_VIDEO_PACE_DS] = {"As on the DS", "Comme sur DS", "Wie auf dem DS", "Come sul DS", "Como en la DS"},
    [TXT_VIDEO_PACE_WARNING] =
        {"Away from the DS's pace the picture can show errors (a scene's camera gliding between "
         "shots, some effects). If something looks wrong, choose As on the DS.",
         "Hors du rythme de la DS, l'image peut avoir des erreurs (la caméra d'une scène qui "
         "glisse entre deux plans, certains effets). Si quelque chose semble faux, choisissez "
         "Comme sur DS.",
         "Abseits der DS-Bildrate kann das Bild Fehler zeigen (die Kamera einer Szene gleitet "
         "zwischen zwei Einstellungen, manche Effekte). Wenn etwas falsch aussieht, wähle Wie "
         "auf dem DS.",
         "Fuori dal ritmo del DS l'immagine può avere errori (la telecamera di una scena che "
         "scivola tra due inquadrature, alcuni effetti). Se qualcosa sembra sbagliato, scegli "
         "Come sul DS.",
         "Fuera del ritmo de la DS la imagen puede tener errores (la cámara de una escena que "
         "se desliza entre dos planos, algunos efectos). Si ves algo raro, elige Como en la DS."},
    [TXT_VIDEO_SHOW_FPS] = {"Show FPS", "Afficher les IPS", "FPS anzeigen", "Mostra FPS", "Mostrar FPS"},
    [TXT_VIDEO_SHOW_FPS_NOTE] = {"The pictures drawn a second, and the game's own frames a second.",
                                 "Les images affichées par seconde, et les images du jeu par seconde.",
                                 "Die gezeichneten Bilder pro Sekunde und die Spielbilder pro Sekunde.",
                                 "Le immagini disegnate al secondo e i fotogrammi del gioco al secondo.",
                                 "Las imágenes dibujadas por segundo y los fotogramas del juego por segundo."},
    [TXT_VIDEO_DISPLAY] = {"Display mode", "Mode d'affichage", "Anzeigemodus", "Modalità schermo", "Modo de pantalla"},
    [TXT_VIDEO_DISPLAY_WINDOW] = {"Window", "Fenêtre", "Fenster", "Finestra", "Ventana"},
    [TXT_VIDEO_DISPLAY_BORDERLESS] = {"Borderless window", "Fenêtre sans bordure", "Randloses Fenster",
                                      "Finestra senza bordi", "Ventana sin bordes"},
    [TXT_VIDEO_DISPLAY_FULLSCREEN] = {"Full screen", "Plein écran", "Vollbild", "Schermo intero", "Pantalla completa"},
    [TXT_VIDEO_DISPLAY_NOTE] =
        {"Window: one you can move and resize. Borderless window: one without borders over the "
         "whole screen. Full screen: the screen for the game alone, at the desktop's resolution.",
         "Fenêtre : déplaçable et redimensionnable. Fenêtre sans bordure : sans bordure, sur tout "
         "l'écran. Plein écran : l'écran pour le jeu seul, à la résolution du bureau.",
         "Fenster: verschieb- und skalierbar. Randloses Fenster: ohne Rand über den ganzen "
         "Bildschirm. Vollbild: der Bildschirm nur für das Spiel, in der Auflösung des Desktops.",
         "Finestra: si può spostare e ridimensionare. Finestra senza bordi: senza bordi su tutto lo "
         "schermo. Schermo intero: lo schermo solo per il gioco, alla risoluzione del desktop.",
         "Ventana: se puede mover y cambiar de tamaño. Ventana sin bordes: sin bordes, ocupando "
         "toda la pantalla. Pantalla completa: la pantalla solo para el juego, a la resolución del "
         "escritorio."},
    [TXT_VIDEO_SUBTITLES] = {"Subtitles", "Sous-titres", "Untertitel", "Sottotitoli", "Subtítulos"},
    [TXT_VIDEO_SUBTITLES_NOTE] = {"The subtitles over the game's movies. A new game asks for it too.",
                                  "Les sous-titres des cinématiques du jeu. Une nouvelle partie le demande aussi.",
                                  "Die Untertitel der Filmsequenzen des Spiels. Ein neues Spiel fragt auch danach.",
                                  "I sottotitoli dei filmati del gioco. Anche una nuova partita lo chiede.",
                                  "Los subtítulos de las películas del juego. También lo pregunta una partida nueva."},
    [TXT_VIDEO_BACKEND] = {"Graphics API", "API graphique", "Grafik-API", "API grafica", "API gráfica"},
    [TXT_VIDEO_BACKEND_AUTO] = {"Automatic", "Automatique", "Automatisch", "Automatica", "Automática"},
    [TXT_VIDEO_BACKEND_NOTE] = {"What draws the picture: Automatic is Direct3D 12 on Windows, Vulkan on a Steam Deck "
                                "or under Proton. If the graphics card stops answering, try Vulkan.",
                                "Ce qui dessine l'image : Automatique est Direct3D 12 sous Windows, Vulkan sur un "
                                "Steam Deck ou sous Proton. Si la carte graphique ne répond plus, essayez Vulkan.",
                                "Was das Bild zeichnet: Automatisch ist unter Windows Direct3D 12, auf einem Steam "
                                "Deck oder unter Proton Vulkan. Wenn die Grafikkarte nicht mehr antwortet, Vulkan "
                                "versuchen.",
                                "Cosa disegna l'immagine: Automatica è Direct3D 12 su Windows, Vulkan su uno Steam "
                                "Deck o con Proton. Se la scheda grafica smette di rispondere, prova Vulkan.",
                                "Lo que dibuja la imagen: Automática es Direct3D 12 en Windows y Vulkan en una Steam "
                                "Deck o con Proton. Si la tarjeta gráfica deja de responder, prueba Vulkan."},
    [TXT_BACKEND_REFUSED] = {"the graphics API %s cannot run here (%s); the system's choice is used",
                             "l'API graphique %s ne peut pas fonctionner ici (%s) ; le choix du système est utilisé",
                             "die Grafik-API %s läuft hier nicht (%s); die Wahl des Systems wird verwendet",
                             "l'API grafica %s non può funzionare qui (%s); si usa la scelta del sistema",
                             "la API gráfica %s no puede funcionar aquí (%s); se usa la elección del sistema"},
    /* the corner's line: the pictures a second, then the game's frames a second */
    [TXT_FPS_LINE] = {"%d FPS  game %d", "%d IPS  jeu %d", "%d FPS  Spiel %d", "%d FPS  gioco %d", "%d FPS  juego %d"},
    [TXT_VIDEO_NEEDS_GPU] = {"Needs the graphics card.", "Nécessite la carte graphique.", "Braucht die Grafikkarte.",
                             "Richiede la scheda grafica.", "Necesita la tarjeta gráfica."},
    [TXT_RESTORE_VIDEO] = {"Restore the video settings", "Rétablir la vidéo", "Video zurücksetzen",
                           "Ripristina il video", "Restablecer el vídeo"},
    [TXT_SOUND] = {"Sound", "Son", "Ton", "Audio", "Sonido"},
    [TXT_SOUND_VOLUME] = {"Volume", "Volume", "Lautstärke", "Volume", "Volumen"},
    [TXT_SOUND_VOLUME_NOTE] = {"The volume of everything the game plays.",
                               "Le volume de tout ce que le jeu fait entendre.",
                               "Die Lautstärke von allem, was das Spiel abspielt.",
                               "Il volume di tutto ciò che il gioco riproduce.",
                               "El volumen de todo lo que suena en el juego."},
    [TXT_SOUND_MUSIC] = {"Music", "Musique", "Musik", "Musica", "Música"},
    [TXT_SOUND_MUSIC_NOTE] = {"The music, streamed and sequenced.", "La musique, en flux comme séquencée.",
                              "Die Musik, gestreamt wie sequenziert.", "La musica, in streaming e sequenziata.",
                              "La música, en streaming y secuenciada."},
    [TXT_SOUND_EFFECTS] = {"Effects", "Effets", "Effekte", "Effetti", "Efectos"},
    [TXT_SOUND_EFFECTS_NOTE] = {"Sound effects, magic, weapons and the world around.",
                                "Les bruitages, la magie, les armes et le monde alentour.",
                                "Soundeffekte, Magie, Waffen und die Umgebung.",
                                "Effetti sonori, magie, armi e il mondo circostante.",
                                "Efectos de sonido, magia, armas y el mundo alrededor."},
    [TXT_SOUND_VOICES] = {"Voices", "Voix", "Stimmen", "Voci", "Voces"},
    [TXT_SOUND_VOICES_NOTE] = {"The characters' voices.", "Les voix des personnages.", "Die Stimmen der Figuren.",
                               "Le voci dei personaggi.", "Las voces de los personajes."},
    [TXT_SOUND_MOVIES] = {"Movies", "Vidéos", "Filmsequenzen", "Filmati", "Vídeos"},
    [TXT_SOUND_MOVIES_NOTE] = {"The movies' music and voices.", "La musique et les voix des vidéos.",
                               "Musik und Stimmen der Filmsequenzen.", "Musica e voci dei filmati.",
                               "La música y las voces de los vídeos."},
    [TXT_SOUND_DS_EXACT] = {"DS-exact sound", "Son exact de la DS", "Ton genau wie der DS", "Audio esatto del DS",
                            "Sonido exacto de la DS"},
    [TXT_SOUND_DS_EXACT_NOTE] = {"The DS's own output: 32,728 Hz and 10 bits, each sample held, as the handheld made "
                                 "it. Off: the PC's, smoothed between the samples.",
                                 "La sortie de la DS : 32 728 Hz et 10 bits, chaque échantillon maintenu, comme la "
                                 "console la produisait. Désactivé : celle du PC, lissée entre les échantillons.",
                                 "Die Ausgabe des DS: 32.728 Hz und 10 Bit, jedes Sample gehalten, wie das Handheld "
                                 "sie erzeugte. Aus: die des PCs, zwischen den Samples geglättet.",
                                 "L'uscita del DS: 32.728 Hz e 10 bit, ogni campione mantenuto, come la produceva la "
                                 "console. Disattivato: quella del PC, levigata tra i campioni.",
                                 "La salida de la DS: 32.728 Hz y 10 bits, cada muestra mantenida, tal como la "
                                 "producía la consola. Desactivado: la del PC, suavizada entre las muestras."},
    [TXT_RESTORE_SOUND] = {"Restore the sound settings", "Rétablir le son", "Ton zurücksetzen", "Ripristina l'audio",
                           "Restablecer el sonido"},
    /* %d: the internal resolution asked for; %s: why it cannot be made. */
    [TXT_SCALE_REFUSED] = {"the graphics card cannot draw at internal resolution %d (%s); the picture stays as it was",
                           "la carte graphique ne peut pas dessiner en résolution interne %d (%s) ; l'image reste "
                           "comme avant",
                           "die Grafikkarte kann nicht mit interner Auflösung %d zeichnen (%s); das Bild bleibt, "
                           "wie es war",
                           "la scheda grafica non può disegnare alla risoluzione interna %d (%s); l'immagine resta "
                           "com'era",
                           "la tarjeta gráfica no puede dibujar con resolución interna %d (%s); la imagen se queda "
                           "como estaba"},
    /* The settings file's [video] section. %s: the setting, the value found,
     * the value used instead. */
    [TXT_VIDEO_BAD_VALUE] = {"settings: [video] %s = \"%s\" is not one of its values; %s is used",
                             "réglages : [video] %s = « %s » n'est pas une valeur possible ; %s est utilisé",
                             "Einstellungen: [video] %s = „%s“ ist kein möglicher Wert; %s wird verwendet",
                             "impostazioni: [video] %s = \"%s\" non è un valore possibile; si usa %s",
                             "ajustes: [video] %s = \"%s\" no es un valor posible; se usa %s"},
    /* %s: the setting. */
    [TXT_VIDEO_UNKNOWN_KEY] = {"settings: [video] has no setting \"%s\"; kept as it is",
                               "réglages : [video] n'a pas de réglage « %s » ; il est conservé tel quel",
                               "Einstellungen: [video] hat keine Einstellung „%s“; sie bleibt erhalten",
                               "impostazioni: [video] non ha l'impostazione \"%s\"; viene conservata",
                               "ajustes: [video] no tiene el ajuste \"%s\"; se conserva tal cual"},
    /* The settings file's [sound] section. %s: the setting, the value found,
     * the value used instead. */
    [TXT_SOUND_BAD_VALUE] = {"settings: [sound] %s = \"%s\" is not one of its values; %s is used",
                             "réglages : [sound] %s = « %s » n'est pas une valeur possible ; %s est utilisé",
                             "Einstellungen: [sound] %s = „%s“ ist kein möglicher Wert; %s wird verwendet",
                             "impostazioni: [sound] %s = \"%s\" non è un valore possibile; si usa %s",
                             "ajustes: [sound] %s = \"%s\" no es un valor posible; se usa %s"},
    /* %s: the setting. */
    [TXT_SOUND_UNKNOWN_KEY] = {"settings: [sound] has no setting \"%s\"; kept as it is",
                               "réglages : [sound] n'a pas de réglage « %s » ; il est conservé tel quel",
                               "Einstellungen: [sound] hat keine Einstellung „%s“; sie bleibt erhalten",
                               "impostazioni: [sound] non ha l'impostazione \"%s\"; viene conservata",
                               "ajustes: [sound] no tiene el ajuste \"%s\"; se conserva tal cual"},
    /* Mission Mode's multiplayer. */
    [TXT_MULTIPLAYER] = {"Multiplayer", "Multijoueur", "Mehrspieler", "Multigiocatore", "Multijugador"},
    [TXT_NICKNAME] = {"Nickname", "Pseudo", "Spitzname", "Soprannome", "Apodo"},
    [TXT_NICKNAME_NOTE] =
        {"The name the other players see in multiplayer, up to 10 characters. Empty: an automatic one.",
         "Le nom que voient les autres joueurs en multijoueur, 10 caractères au plus. Vide : un nom "
         "automatique.",
         "Der Name, den die anderen Spieler im Mehrspielermodus sehen, höchstens 10 Zeichen. Leer: "
         "ein automatischer.",
         "Il nome che vedono gli altri giocatori in multigiocatore, fino a 10 caratteri. Vuoto: uno "
         "automatico.",
         "El nombre que ven los demás jugadores en multijugador, hasta 10 caracteres. Vacío: uno "
         "automático."},
    [TXT_ONLINE] = {"Online play", "Jeu en ligne", "Online-Spiel", "Gioco online", "Juego en línea"},
    [TXT_ONLINE_NOTE] = {"Play with others anywhere, through the server; on this PC and the local network no server is "
                         "needed.",
                         "Jouer avec d'autres n'importe où, par le serveur ; sur ce PC et le réseau local, aucun "
                         "serveur n'est nécessaire.",
                         "Mit anderen überall spielen, über den Server; auf diesem PC und im lokalen Netzwerk ist kein "
                         "Server nötig.",
                         "Giocare con altri ovunque, tramite il server; su questo PC e nella rete locale non serve "
                         "alcun server.",
                         "Jugar con otros desde cualquier sitio, a través del servidor; en este PC y en la red local "
                         "no hace falta servidor."},
    [TXT_SERVER] = {"Server", "Serveur", "Server", "Server", "Servidor"},
    [TXT_SERVER_NOTE] = {"The address of the khdays-server to meet through (host, or host:port). Anyone can run one.",
                         "L'adresse du khdays-server par lequel se retrouver (hôte, ou hôte:port). N'importe qui peut "
                         "en lancer un.",
                         "Die Adresse des khdays-server, über den man sich trifft (Host oder Host:Port). Jeder kann "
                         "einen betreiben.",
                         "L'indirizzo del khdays-server tramite cui incontrarsi (host, o host:porta). Chiunque può "
                         "avviarne uno.",
                         "La dirección del khdays-server por el que encontrarse (host, o host:puerto). Cualquiera "
                         "puede abrir uno."},
    [TXT_CHECK_UPDATES] = {"Check for updates", "Rechercher des mises à jour", "Nach Updates suchen",
                           "Cerca aggiornamenti", "Buscar actualizaciones"},
    [TXT_CHECK_UPDATES_NOTE] = {"Once a day, asks GitHub whether a newer version of the port is out, and says so on "
                                "the title screen. Nothing about you is sent.",
                                "Une fois par jour, demande à GitHub si une version plus récente du portage est "
                                "sortie, et le signale sur l'écran titre. Rien de vous n'est envoyé.",
                                "Fragt einmal am Tag bei GitHub, ob eine neuere Version der Portierung erschienen "
                                "ist, und meldet es auf dem Titelbildschirm. Nichts über dich wird gesendet.",
                                "Una volta al giorno chiede a GitHub se è uscita una versione più recente del port e "
                                "lo segnala nella schermata del titolo. Non viene inviato nulla su di te.",
                                "Una vez al día pregunta a GitHub si ha salido una versión más nueva del port, y lo "
                                "avisa en la pantalla de título. No se envía nada tuyo."},
    [TXT_UPDATE_TITLE] = {"A new version of the port is out", "Une nouvelle version du portage est sortie",
                          "Eine neue Version der Portierung ist erschienen", "È uscita una nuova versione del port",
                          "Ha salido una versión nueva del port"},
    [TXT_UPDATE_DETAIL] = {"You have %s; %s is out. Its installer updates this copy and keeps your saves, "
                           "settings and mods.",
                           "Vous avez la %s ; la %s est sortie. Son installateur met à jour cette copie et garde "
                           "vos sauvegardes, réglages et mods.",
                           "Du hast %s; %s ist erschienen. Ihr Installationsprogramm aktualisiert diese Kopie und "
                           "behält deine Spielstände, Einstellungen und Mods.",
                           "Hai la %s; è uscita la %s. Il suo programma di installazione aggiorna questa copia e "
                           "conserva salvataggi, impostazioni e mod.",
                           "Tienes la %s; ha salido la %s. Su instalador actualiza esta copia y conserva tus "
                           "partidas, ajustes y mods."},
    [TXT_UPDATE_DOWNLOAD] = {"Download", "Télécharger", "Herunterladen", "Scarica", "Descargar"},
    [TXT_UPDATE_LATER] = {"Later", "Plus tard", "Später", "Più tardi", "Más tarde"},
    [TXT_UPDATE_SKIP] = {"Don't remind me of this version", "Ne plus me le rappeler pour cette version",
                         "Für diese Version nicht mehr erinnern", "Non ricordarmelo per questa versione",
                         "No avisar de esta versión"},
    [TXT_UPDATE_DETAIL_AUTO] = {"You have %s; %s is out. Download installs it: the game closes, the installer "
                                "updates this copy, keeping your saves, settings and mods, and opens the game again.",
                                "Vous avez la %s ; la %s est sortie. Télécharger l'installe : le jeu se ferme, "
                                "l'installateur met à jour cette copie en gardant vos sauvegardes, réglages et mods, "
                                "puis rouvre le jeu.",
                                "Du hast %s; %s ist erschienen. Herunterladen installiert sie: Das Spiel schließt "
                                "sich, das Installationsprogramm aktualisiert diese Kopie, behält deine Spielstände, "
                                "Einstellungen und Mods und öffnet das Spiel wieder.",
                                "Hai la %s; è uscita la %s. Scarica la installa: il gioco si chiude, il programma di "
                                "installazione aggiorna questa copia conservando salvataggi, impostazioni e mod, e "
                                "riapre il gioco.",
                                "Tienes la %s; ha salido la %s. Descargar la instala: el juego se cierra, el "
                                "instalador actualiza esta copia conservando tus partidas, ajustes y mods, y vuelve a "
                                "abrir el juego."},
    [TXT_UPDATE_GETTING] = {"Downloading %s", "Téléchargement de la %s", "%s wird heruntergeladen", "Download della %s",
                            "Descargando la %s"},
    [TXT_UPDATE_PROGRESS] = {"%d%% (%.1f of %.1f MB)", "%d %% (%.1f sur %.1f Mo)", "%d %% (%.1f von %.1f MB)",
                             "%d%% (%.1f di %.1f MB)", "%d %% (%.1f de %.1f MB)"},
    [TXT_UPDATE_FAILED] = {"The update could not be installed", "La mise à jour n'a pas pu être installée",
                           "Das Update konnte nicht installiert werden",
                           "Non è stato possibile installare l'aggiornamento", "No se pudo instalar la actualización"},
    [TXT_UPDATE_FAILED_DETAIL] = {"Its page has the installer, to download and run by hand. (%s)",
                                  "Sa page propose l'installateur, à télécharger et lancer vous-même. (%s)",
                                  "Auf ihrer Seite liegt das Installationsprogramm, zum Herunterladen und Ausführen "
                                  "von Hand. (%s)",
                                  "La sua pagina ha il programma di installazione, da scaricare e avviare a mano. (%s)",
                                  "Su página tiene el instalador, para descargarlo y ejecutarlo a mano. (%s)"},
    [TXT_UPDATE_OPEN_PAGE] = {"Open its page", "Ouvrir sa page", "Seite öffnen", "Apri la pagina", "Abrir su página"},
    [TXT_SHOW_PING] = {"Show ping", "Afficher le ping", "Ping anzeigen", "Mostra ping", "Mostrar ping"},
    [TXT_SHOW_PING_NOTE] =
        {"In a multiplayer session, the round trip to each other player in milliseconds, in a "
         "corner.",
         "En partie multijoueur, l'aller-retour vers chaque autre joueur en millisecondes, dans un "
         "coin.",
         "In einer Mehrspielersitzung die Umlaufzeit zu jedem anderen Spieler in Millisekunden, in "
         "einer Ecke.",
         "In una sessione multigiocatore, il tempo di andata e ritorno verso ogni altro giocatore in "
         "millisecondi, in un angolo.",
         "En una partida multijugador, el tiempo de ida y vuelta con cada otro jugador en "
         "milisegundos, en una esquina."},
    /* the numbers (each member's, "-" while not measured), then "ms" */
    [TXT_PING_LINE] = {"Ping %s ms", "Ping %s ms", "Ping %s ms", "Ping %s ms", "Ping %s ms"},
    [TXT_ROOM_SEARCH] = {"Search by name", "Rechercher par nom", "Nach Namen suchen", "Cerca per nome",
                         "Buscar por nombre"},
    [TXT_ROOM_NAME] = {"Room name", "Nom du salon", "Name des Raums", "Nome della stanza", "Nombre de la sala"},
    [TXT_ROOM_PASSWORD] = {"Password (optional)", "Mot de passe (facultatif)", "Passwort (optional)",
                           "Password (facoltativa)", "Contraseña (opcional)"},
    [TXT_ROOM_PASSWORD_TAG] = {"Password", "Mot de passe", "Passwort", "Password", "Contraseña"},
    [TXT_ROOM_PASSWORD_ASK] = {"This room asks for a password.", "Ce salon demande un mot de passe.",
                               "Dieser Raum verlangt ein Passwort.", "Questa stanza chiede una password.",
                               "Esta sala pide contraseña."},
    [TXT_ROOM_PASSWORD_NOTE] = {"With a password, only those who know it get in: tell it to whoever you want.",
                                "Avec un mot de passe, seuls ceux qui le connaissent entrent : "
                                "dites-le à qui vous voulez.",
                                "Mit einem Passwort kommt nur herein, wer es kennt: Sag es, wem du willst.",
                                "Con una password entra solo chi la conosce: dilla a chi vuoi.",
                                "Con contraseña solo entra quien la sepa: díselo a quien quieras."},
    [TXT_ROOM_WRONG_PASSWORD] = {"Wrong password.", "Mot de passe incorrect.", "Falsches Passwort.", "Password errata.",
                                 "Contraseña incorrecta."},
    [TXT_ROOM_NONE] = {"No rooms.", "Aucun salon.", "Keine Räume.", "Nessuna stanza.", "No hay salas."},
    [TXT_ROOM_JOIN] = {"Join", "Rejoindre", "Beitreten", "Unisciti", "Unirse"},
    [TXT_ROOM_HOST] = {"Host: %s", "Hôte : %s", "Host: %s", "Host: %s", "Anfitrión: %s"},
    [TXT_ROOM_PLAYERS] = {"%d/%d players", "%d/%d joueurs", "%d/%d Spieler", "%d/%d giocatori", "%d/%d jugadores"},
    [TXT_ROOM_HOST_TAG] = {"Host", "Hôte", "Host", "Host", "Anfitrión"},
    /* a group's places as the game's own bars label them (UI/mlt/res_&.p2) */
    [TXT_ROOM_PLACE] = {"%dP", "J%d", "S%d", "G%d", "J%d"},
    [TXT_ROOM_YOU] = {"You", "Vous", "Du", "Tu", "Tú"},
    [TXT_ROOM_LEVEL] = {"Lv. %d", "Niv. %d", "Lv. %d", "Liv. %d", "Nv. %d"},
    [TXT_SELECT_RANDOM] = {"Random", "Aléatoire", "Zufällig", "Casuale", "Aleatorio"},
    [TXT_NEEDS_GAME_DATA] =
        {"The game's data is not here. The port looks for it in:\n%s\n\nExtract it from your own copy of "
         "the game with tools/extract_data.py (the README's \"Getting started\"), and start the "
         "port from the folder that holds data.",
         "Les données du jeu sont introuvables. Le portage les cherche dans :\n%s\n\nExtrayez-les de votre "
         "propre copie du jeu avec tools/extract_data.py (« Getting started » dans le README), "
         "et lancez le portage depuis le dossier qui contient data.",
         "Die Daten des Spiels fehlen. Der Port sucht sie in:\n%s\n\nExtrahiere sie aus deiner eigenen "
         "Kopie des Spiels mit tools/extract_data.py (\"Getting started\" in der README) und "
         "starte den Port aus dem Ordner, der data enthält.",
         "I dati del gioco non ci sono. Il port li cerca in:\n%s\n\nEstraili dalla tua copia del gioco con "
         "tools/extract_data.py (\"Getting started\" nel README) e avvia il port dalla cartella "
         "che contiene data.",
         "No se encuentran los datos del juego. El port los busca en:\n%s\n\nExtráelos de tu propia copia "
         "del juego con tools/extract_data.py (\"Getting started\" en el README) y abre el port "
         "desde la carpeta que contiene data."},
    [TXT_GAME_DATA_BROKEN] =
        {"The game's data in\n%s\nis incomplete or damaged: %s\n\nExtract it again from your own copy of "
         "the game with tools/extract_data.py.",
         "Les données du jeu dans\n%s\nsont incomplètes ou abîmées : %s\n\nExtrayez-les à nouveau de "
         "votre propre copie du jeu avec tools/extract_data.py.",
         "Die Daten des Spiels in\n%s\nsind unvollständig oder beschädigt: %s\n\nExtrahiere sie erneut "
         "aus deiner eigenen Kopie des Spiels mit tools/extract_data.py.",
         "I dati del gioco in\n%s\nsono incompleti o danneggiati: %s\n\nEstraili di nuovo dalla tua copia "
         "del gioco con tools/extract_data.py.",
         "Los datos del juego en\n%s\nestán incompletos o dañados: %s\n\nVuelve a extraerlos de tu propia "
         "copia del juego con tools/extract_data.py."},
    [TXT_PAUSE_RETRY] = {"Retry", "Réessayer", "Wiederholen", "Riprova", "Reintentar"},
    [TXT_PANELS_STATS] = {"Stats", "Statistiques", "Status", "Statistiche", "Estadísticas"},
    [TXT_PANELS_SUBMENU] = {"Submenu", "Sous-menu", "Untermenü", "Sottomenu", "Submenú"},
    [TXT_VIEWER_MODEL] = {"3D Model", "Modèle 3D", "3D-Modell", "Modello 3D", "Modelo 3D"},
    [TXT_VIEWER_TURN] = {"Turn", "Tourner", "Drehen", "Ruota", "Girar"},
    [TXT_VIEWER_ZOOM] = {"Zoom", "Zoom", "Zoom", "Zoom", "Zoom"},
    [TXT_VIEWER_RESET] = {"Reset", "Recentrer", "Zurücksetzen", "Ripristina", "Restablecer"},
    [TXT_VIEWER_BACK] = {"Back", "Retour", "Zurück", "Indietro", "Volver"},
    [TXT_VIEWER_ENEMY] = {"Enemy", "Ennemi", "Gegner", "Nemico", "Enemigo"},
    [TXT_SAVE_AUTOSAVE_TAG] = {"AUTO", "AUTO", "AUTO", "AUTO", "AUTO"},
    [TXT_CAMP_BACK_TO_MENU] = {"Return to the main menu?", "Revenir au menu principal ?", "Zum Hauptmenü zurückkehren?",
                               "Tornare al menu principale?", "¿Volver al menú principal?"},
    [TXT_HINTS_TEXT_KEYBOARD] = {"Type it   Backspace: erase   Enter: done   Esc: cancel",
                                 "Tapez-le   Retour arrière : effacer   Entrée : valider   Échap : annuler",
                                 "Eintippen   Rücktaste: löschen   Eingabe: fertig   Esc: abbrechen",
                                 "Scrivilo   Backspace: cancella   Invio: fatto   Esc: annulla",
                                 "Escríbelo   Retroceso: borrar   Intro: hecho   Esc: cancelar"},
    /* %s: the gamepad's accept, clear and back buttons. */
    [TXT_HINTS_TEXT_GAMEPAD] = {"Up/Down: character   Right: add   Left: erase   %s: done   %s: clear all   %s: cancel",
                                "Haut/Bas : caractère   Droite : ajouter   Gauche : effacer   %s : valider   %s : tout "
                                "effacer   %s : annuler",
                                "Oben/Unten: Zeichen   Rechts: anfügen   Links: löschen   %s: fertig   %s: alles "
                                "löschen   %s: abbrechen",
                                "Su/Giù: carattere   Destra: aggiungi   Sinistra: cancella   %s: fatto   %s: cancella "
                                "tutto   %s: annulla",
                                "Arriba/Abajo: carácter   Derecha: añadir   Izquierda: borrar   %s: hecho   %s: borrar "
                                "todo   %s: cancelar"},
    /* The game's own words (its Config page); the circled X it draws is written X. */
    [TXT_GAME_CONFIG] = {"Config", "Configuration", "Konfig.", "Configura", "Preferencias"},
    [TXT_GAME_CONTROLS] = {"Controls", "Commandes", "Steuerung", "Comandi", "Controles"},
    [TXT_GAME_TYPE_A] = {"Type A", "Type A", "Typ A", "Tipo A", "Tipo A"},
    [TXT_GAME_COMMAND_LIST] = {"Command List", "Liste commandes", "Befehlsliste", "Elenco com.", "Comandos"},
    [TXT_GAME_ONLY] = {"X Only", "Seul X", "Nur X", "Solo X", "Sólo X"},
    /* The one-screen menu. */
    [TXT_GENERAL] = {"General", "Général", "Allgemein", "Generale", "General"},
    [TXT_TAKES_NEXT_MOVIE] = {"Takes effect from the next movie.", "S'applique à partir de la prochaine vidéo.",
                              "Gilt ab dem nächsten Video.", "Vale dal prossimo filmato.",
                              "Se aplica desde el próximo vídeo."},
    [TXT_TAKES_RESUME] = {"Takes effect when the game resumes.", "S'applique à la reprise du jeu.",
                          "Gilt, sobald das Spiel weiterläuft.", "Vale alla ripresa del gioco.",
                          "Se aplica al reanudar el juego."},
    [TXT_TAKES_NEXT_SESSION] = {"Takes effect from the next multiplayer session.",
                                "S'applique à partir de la prochaine session multijoueur.",
                                "Gilt ab der nächsten Mehrspielersitzung.",
                                "Vale dalla prossima sessione multigiocatore.",
                                "Se aplica desde la próxima sesión multijugador."},
    /* %s: the action the input does, or TXT_NOTHING. */
    [TXT_TEST_PRESS] = {"Press a key or a button: %s", "Appuyez sur une touche ou un bouton : %s",
                        "Taste oder Knopf drücken: %s", "Premi un tasto o un pulsante: %s",
                        "Pulsa una tecla o un botón: %s"},
    [TXT_MODS] = {"Mods", "Mods", "Mods", "Mod", "Mods"},
    [TXT_MOD_CHOOSE] = {"Choose", "Choisir", "Auswählen", "Scegli", "Elegir"},
    [TXT_MOD_ENABLED] = {"Enabled", "Activé", "Aktiviert", "Attivo", "Activado"},
    [TXT_MOD_EARLIER] = {"Load earlier", "Charger avant", "Früher laden", "Carica prima", "Cargar antes"},
    [TXT_MOD_LATER] = {"Load later", "Charger après", "Später laden", "Carica dopo", "Cargar después"},
    [TXT_MOD_RESUME] = {"Resume", "Reprendre", "Fortsetzen", "Riprendi", "Reanudar"},
    [TXT_MOD_RUNNING] = {"Running", "En cours", "Läuft", "In esecuzione", "En marcha"},
    [TXT_MOD_OFF] = {"Off", "Désactivé", "Aus", "Disattivato", "Desactivado"},
    [TXT_MOD_STOPPED] = {"Stopped by an error: %s", "Arrêté par une erreur : %s", "Durch einen Fehler gestoppt: %s",
                         "Fermato da un errore: %s", "Detenido por un error: %s"},
    [TXT_MOD_PAUSED] = {"Paused: %s", "En pause : %s", "Pausiert: %s", "In pausa: %s", "En pausa: %s"},
    [TXT_MOD_REFUSED] = {"Not loaded: %s", "Non chargé : %s", "Nicht geladen: %s", "Non caricato: %s",
                         "No cargado: %s"},
    [TXT_MOD_ORDER_NOTE] = {"Mods later in the list run after the earlier ones: their changes stand.",
                            "Les mods plus bas dans la liste passent après les autres : leurs changements l'emportent.",
                            "Spätere Mods der Liste laufen nach den früheren: ihre Änderungen gelten.",
                            "I mod più in basso nell'elenco vengono dopo gli altri: valgono le loro modifiche.",
                            "Los mods más abajo en la lista van después de los demás: sus cambios prevalecen."},
    [TXT_ROOM_NEEDS_MODS] = {"This room plays with these mods: %s", "Ce salon joue avec ces mods : %s",
                             "Dieser Raum spielt mit diesen Mods: %s", "Questa stanza gioca con questi mod: %s",
                             "Esta sala juega con estos mods: %s"},
    [TXT_MOD_HOST_SETS] = {"In a room, the host's options are the ones that count.",
                           "Dans un salon, ce sont les options de l'hôte qui comptent.",
                           "In einem Raum gelten die Optionen des Hosts.",
                           "In una stanza contano le opzioni dell'host.",
                           "En una sala cuentan las opciones del anfitrión."},
    [TXT_MOD_WRITES_MEMORY] = {"Writes in the game's memory: a mistake of its own can stop the game or reach a save.",
                               "Écrit dans la mémoire du jeu : une erreur de sa part peut arrêter le jeu ou "
                               "atteindre une sauvegarde.",
                               "Schreibt in den Speicher des Spiels: ein Fehler darin kann das Spiel anhalten "
                               "oder einen Spielstand erreichen.",
                               "Scrive nella memoria del gioco: un suo errore può fermare il gioco o finire in un "
                               "salvataggio.",
                               "Escribe en la memoria del juego: un error suyo puede detener el juego o llegar a una "
                               "partida guardada."},
    [TXT_AUTOSAVE] = {"Autosave", "Sauvegarde auto", "Automatisch speichern", "Salvataggio automatico", "Autoguardado"},
    [TXT_AUTOSAVE_FILE99] = {"In file 99", "Dans le fichier 99", "In Datei 99", "Nel file 99", "En el archivo 99"},
    [TXT_AUTOSAVE_PLAYED] = {"In the file being played", "Dans le fichier en cours", "In der gespielten Datei",
                             "Nel file in gioco", "En el archivo en juego"},
    [TXT_AUTOSAVE_NOTE] = {"After each completed mission, once the game is back where it lets you save: into file 99 "
                           "(AUTO in the lists), or into the file you are playing (a game never saved: the first "
                           "empty file).",
                           "Après chaque mission réussie, quand le jeu est de retour là où il laisse sauvegarder : "
                           "dans le fichier 99 (AUTO dans les listes), ou dans le fichier en cours (une partie jamais "
                           "sauvegardée : le premier fichier vide).",
                           "Nach jeder geschafften Mission, sobald das Spiel wieder dort ist, wo es speichern lässt: "
                           "in Datei 99 (AUTO in den Listen) oder in die gespielte Datei (ein nie gespeichertes "
                           "Spiel: die erste leere Datei).",
                           "Dopo ogni missione completata, quando il gioco torna dove lascia salvare: nel file 99 "
                           "(AUTO negli elenchi) o nel file in gioco (una partita mai salvata: il primo file vuoto).",
                           "Tras cada misión completada, cuando el juego vuelve a donde deja guardar: en el archivo 99 "
                           "(AUTO en las listas) o en el archivo en juego (una partida nunca guardada: el primer "
                           "archivo vacío)."},
    [TXT_SAVE_FILES] = {"Files in the game's lists", "Fichiers dans les listes du jeu",
                        "Dateien in den Listen des Spiels", "File negli elenchi del gioco",
                        "Archivos en las listas del juego"},
    [TXT_SAVE_FILES_ALL] = {"All 99", "Les 99", "Alle 99", "Tutti i 99", "Los 99"},
    [TXT_SAVE_FILES_DS] = {"3, as on the DS", "3, comme sur DS", "3, wie auf dem DS", "3, come su DS", "3, como en DS"},
    [TXT_SAVE_FILES_NOTE] = {"The game's own save and load lists (without the HD save screens): the port's 99 files in "
                             "pages of three, or the DS's three files.",
                             "Les listes de sauvegarde du jeu (sans les écrans HD) : les 99 fichiers du port par pages "
                             "de trois, ou les trois fichiers de la DS.",
                             "Die Speicherlisten des Spiels (ohne die HD-Bildschirme): die 99 Dateien des Ports in "
                             "Seiten zu drei, oder die drei Dateien des DS.",
                             "Gli elenchi di salvataggio del gioco (senza le schermate HD): i 99 file del port in "
                             "pagine da tre, o i tre file del DS.",
                             "Las listas de guardado del propio juego (sin las pantallas HD): los 99 archivos del port "
                             "en páginas de tres, o los tres archivos de la DS."},
    [TXT_STOP_MAGIC] = {"Stop magic", "Magie Stop", "Stopp-Magie", "Magia Stop", "Magia Paro"},
    [TXT_STOP_MAGIC_NOTE] = {"The magic Days left unfinished, restored: its panels, its recipes at the moogle's and "
                             "its casts. Off, the game is the DS's; a save's Stop panels are kept, unseen. In a room, "
                             "every player must have it the same.",
                             "La magie que Days a laissée inachevée, restaurée : ses panneaux, ses recettes chez le "
                             "Mog et ses sorts. Désactivée, le jeu est celui de la DS ; les panneaux Stop d'une "
                             "sauvegarde sont gardés, invisibles. Dans un salon, tous les joueurs doivent l'avoir "
                             "pareil.",
                             "Die Magie, die Days unvollendet ließ, wiederhergestellt: ihre Felder, ihre Rezepte beim "
                             "Mogry und ihre Zauber. Aus ist das Spiel das des DS; Stopp-Felder eines Spielstands "
                             "bleiben erhalten, unsichtbar. In einem Raum müssen es alle Spieler gleich haben.",
                             "La magia che Days ha lasciato incompiuta, ripristinata: i suoi pannelli, le sue ricette "
                             "dal moguri e i suoi incantesimi. Disattivata, il gioco è quello del DS; i pannelli Stop "
                             "di un salvataggio restano, nascosti. In una stanza tutti i giocatori devono averla "
                             "uguale.",
                             "La magia que Days dejó sin terminar, restaurada: sus paneles, sus recetas en el moguri y "
                             "sus lanzamientos. Desactivada, el juego es el de la DS; los paneles de Paro de una "
                             "partida se conservan, ocultos. En una sala todos los jugadores deben tenerla igual."},
    [TXT_STOP_MAGIC_ON] = {"Stop magic on", "Magie Stop activée", "Stopp-Magie an", "Magia Stop attiva",
                           "Magia Paro activada"},
    [TXT_STOP_MAGIC_OFF] = {"Stop magic off", "Magie Stop désactivée", "Stopp-Magie aus", "Magia Stop disattivata",
                            "Magia Paro desactivada"},
    [TXT_TAKES_NEXT_START] = {"Takes effect the next time the game starts.", "S'applique au prochain lancement du jeu.",
                              "Gilt beim nächsten Start des Spiels.", "Vale al prossimo avvio del gioco.",
                              "Se aplica la próxima vez que se inicie el juego."},
    [TXT_AUTOSAVE_DONE] = {"Autosaved: file %d", "Sauvegarde auto : fichier %d", "Automatisch gespeichert: Datei %d",
                           "Salvataggio automatico: file %d", "Autoguardado: archivo %d"},
    [TXT_AUTOSAVE_FAILED] = {"Autosave failed: file %d", "Échec de la sauvegarde auto : fichier %d",
                             "Autospeichern fehlgeschlagen: Datei %d", "Salvataggio automatico non riuscito: file %d",
                             "Error al autoguardar: archivo %d"},
    [TXT_ENH_MOUSE] = {"Mouse camera", "Caméra à la souris", "Kamera mit der Maus", "Telecamera con il mouse",
                       "Cámara con el ratón"},
    [TXT_ENH_MOUSE_NOTE] = {"Moving the mouse turns the camera, up and down too. The window holds the mouse while "
                            "you play; Esc frees it with the settings.",
                            "Bouger la souris tourne la caméra, aussi vers le haut et le bas. La fenêtre garde la "
                            "souris pendant le jeu ; Échap la libère avec les réglages.",
                            "Die Maus bewegen dreht die Kamera, auch hoch und runter. Das Fenster hält die Maus beim "
                            "Spielen; Esc gibt sie mit den Einstellungen frei.",
                            "Muovere il mouse gira la telecamera, anche su e giù. La finestra trattiene il mouse "
                            "mentre giochi; Esc lo libera con le impostazioni.",
                            "Mover el ratón gira la cámara, también arriba y abajo. La ventana retiene el ratón "
                            "mientras juegas; Esc lo suelta con los ajustes."},
    [TXT_MOUSE_SPEED] = {"Mouse camera speed (%)", "Vitesse caméra souris (%)", "Maus-Kameratempo (%)",
                         "Velocità telecamera mouse (%)", "Velocidad cámara ratón (%)"},
    [TXT_SHOP_WELCOME] = {"Welcome!", "Bienvenue !", "Willkommen!", "Benvenuto!", "¡Bienvenido!"},
};

static int current = KHDAYS_LANGUAGE_ENGLISH;
static int chosen; /* 0 until a language is set: the system's is taken at first use */

void khdays_ui_set_language(int language)
{
    if (language >= KHDAYS_LANGUAGE_ENGLISH && language <= KHDAYS_LANGUAGE_LAST) {
        current = language;
        chosen = 1;
    }
}

int khdays_ui_language(void)
{
    if (!chosen) {
        khdays_ui_set_language(khdays_language_choose(KHDAYS_LANGUAGE_SYSTEM));
    }
    return current;
}

const char *khdays_text_in(int id, int language)
{
    if (id < 0 || id >= TXT_COUNT || language < KHDAYS_LANGUAGE_ENGLISH || language > KHDAYS_LANGUAGE_LAST) {
        return NULL;
    }
    return texts[id][language - 1];
}

const char *khdays_text(int id)
{
    const char *s = khdays_text_in(id, khdays_ui_language());
    if (s == NULL) {
        s = khdays_text_in(id, KHDAYS_LANGUAGE_ENGLISH);
    }
    return s != NULL ? s : "?";
}

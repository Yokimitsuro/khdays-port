// The game's texts that speak of the DS's hardware (its wireless, power switch, Game Card), in the
// port's words for a PC game (text_patches.h). The string tables are served with those records
// replaced, in all five languages, only where a record still holds the game's own words (a mod's
// text stays the mod's). The game's words are known here only by a digest.
#include "text_patches.h"

#include "khdays/assets/message.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace {

struct Patch {
    const char *path; // as the game's FNT names it
    unsigned index;   // the record
    unsigned game;    // the game's words: FNV-1a over the record's UTF-16LE bytes
    const char16_t *port;
};

// The digest of a record's words (its terminator not included).
unsigned digest(const std::u16string &text)
{
    unsigned h = 2166136261u;
    for (const char16_t c : text) {
        h = (h ^ static_cast<unsigned>(c & 0xff)) * 16777619u;
        h = (h ^ static_cast<unsigned>(c >> 8)) * 16777619u;
    }
    return h;
}

// Each record: the file, the record, the digest of the game's words, the port's words (no longer than
// the game's, with its line breaks where its box needs them).
const Patch patches[] = {
    // Before Mission Mode's multiplayer (the title, ov000).
    {"UI/load/lrd_en.s.z", 16, 0x6b665111u, u"Begin network play?"},
    {"UI/load/lrd_es.s.z", 16, 0xaf94920au, u"¿Iniciar el juego en red?"},
    {"UI/load/lrd_de.s.z", 16, 0x5d2c2e02u, u"Netzwerkspiel starten?"},
    {"UI/load/lrd_fr.s.z", 16, 0x28f55b53u, u"Lancer le jeu en réseau ?"},
    {"UI/load/lrd_it.s.z", 16, 0x20444516u, u"Avviare il gioco in rete?"},
    // The connection error after a reset (scene 12, ov010).
    {"UI/sys/sys_en.s.z", 1, 0x94f712b8u,
     u"Unable to connect to the network.\nPress ㍉ to return to the Title Screen."},
    {"UI/sys/sys_es.s.z", 1, 0x529bd164u,
     u"No se ha podido realizar\nla conexión de red.\n㍉ Ir la pantalla principal."},
    {"UI/sys/sys_de.s.z", 1, 0xd69128d1u,
     u"Die Netzwerkverbindung\nkann nicht hergestellt werden.\nDrücke ㍉, um zum Titelbildschirm\nzurückzukehren."},
    {"UI/sys/sys_fr.s.z", 1, 0xb33de8aeu,
     u"Impossible d'établir la connexion réseau.\nAppuyez sur ㍉ pour revenir à l'écran-titre."},
    {"UI/sys/sys_it.s.z", 1, 0xeb198c48u,
     u"Impossibile stabilire la connessione\ndi rete. Premi ㍉ per tornare \nallo schermo del titolo."},
    // Mission Mode's multiplayer menus (ov006, ov008).
    {"UI/mlt/mlt_en.s.z", 75, 0x2471edd6u, u"Communication error.\nPlease try again."},
    {"UI/mlt/mlt_es.s.z", 75, 0x98a49968u, u"Error de conexión.\nVuelve a intentarlo."},
    {"UI/mlt/mlt_de.s.z", 75, 0x9edfea7au, u"Fehler bei der Datenübertragung.\nBitte versuche es erneut."},
    {"UI/mlt/mlt_fr.s.z", 75, 0xe63a1707u, u"Erreur de communication.\nVeuillez réessayer."},
    {"UI/mlt/mlt_it.s.z", 75, 0xd0658a99u, u"Errore di comunicazione.\nRiprova."},
    // The save list's deleting and its read failures (the title, ov000).
    {"UI/load/lrd_en.s.z", 9, 0xe43e7688u, u"Deleting data...\nDo not close the game."},
    {"UI/load/lrd_es.s.z", 9, 0x5efc5d21u, u"Borrando archivo...\nNo cierres el juego."},
    {"UI/load/lrd_de.s.z", 9, 0xea66d79du, u"Daten werden gelöscht ...\nDas Spiel nicht beenden."},
    {"UI/load/lrd_fr.s.z", 9, 0xd648ae23u, u"Suppression en cours...\nVeuillez ne pas quitter le jeu."},
    {"UI/load/lrd_it.s.z", 9, 0xbae12f65u, u"Cancellazione in corso...\nNon chiudere il gioco."},
    {"UI/load/lrd_en.s.z", 15, 0x76f57a06u, u"Unable to read the data in this slot.\nRestart the game and try again."},
    {"UI/load/lrd_es.s.z", 15, 0x021c5e54u,
     u"El archivo no se ha cargado\ncorrectamente. Reinicia el juego\ny vuelve a intentarlo."},
    {"UI/load/lrd_de.s.z", 15, 0x28cea80du,
     u"Die Daten konnten nicht fehlerfrei geladen werden.\nStarte das Spiel neu und versuche es erneut."},
    {"UI/load/lrd_fr.s.z", 15, 0x61005cd4u,
     u"Échec du chargement du fichier.\nVeuillez redémarrer le jeu et\nréessayer."},
    {"UI/load/lrd_it.s.z", 15, 0xaff67d70u, u"Impossibile leggere i dati nello slot.\nRiavvia il gioco e\nriprova."},
    {"UI/load/lrd_en.s.z", 21, 0xc2d33ed6u, u"Restart the game and try again."},
    {"UI/load/lrd_es.s.z", 21, 0xe70fe0f8u, u"Reinicia el juego y vuelve a intentarlo."},
    {"UI/load/lrd_de.s.z", 21, 0x30e888a2u, u"Spiel neu starten und erneut versuchen."},
    {"UI/load/lrd_fr.s.z", 21, 0x0ff965f6u, u"Veuillez redémarrer le jeu et\nréessayer."},
    {"UI/load/lrd_it.s.z", 21, 0x506010f6u, u"Riavvia il gioco e\nriprova."},
    {"UI/load/lrd_en.s.z", 23, 0xa7c916b5u, u"Initializing...\nDo not close the game."},
    {"UI/load/lrd_es.s.z", 23, 0x7ec938dcu, u"Iniciando...\nNo cierres el juego."},
    {"UI/load/lrd_de.s.z", 23, 0x53bb3672u, u"Initialisiert ...\nDas Spiel nicht beenden."},
    {"UI/load/lrd_fr.s.z", 23, 0xcff44c33u, u"Initialisation en cours...\nVeuillez ne pas quitter le jeu."},
    {"UI/load/lrd_it.s.z", 23, 0x74333b94u, u"Inizializzazione in corso. \nNon chiudere il gioco."},
    // The camp's save screen (ov008, ov009, ov025).
    {"UI/cm/str/sav_en.s.z", 10, 0x565c7cf4u, u"Unable to write data to this slot.\nRestart the game and try again."},
    {"UI/cm/str/sav_es.s.z", 10, 0x6e724628u, u"Archivo mal guardado.\nReinicia el juego y\nvuelve a intentarlo."},
    {"UI/cm/str/sav_de.s.z", 10, 0xc1cbdbbfu, u"Fehler beim Speichern.\nStarte das Spiel neu\nund versuche es erneut."},
    {"UI/cm/str/sav_fr.s.z", 10, 0x19d410b8u,
     u"Échec de la sauvegarde du fichier.\nVeuillez redémarrer le jeu et\nréessayer."},
    {"UI/cm/str/sav_it.s.z", 10, 0x0dddcfa4u, u"Impossibile scrivere i dati nello slot.\nRiavvia il gioco e\nriprova."},
    {"UI/cm/str/sav_en.s.z", 11, 0x50a8ae50u, u"Saving...\nDo not close the game."},
    {"UI/cm/str/sav_es.s.z", 11, 0x1649cc1fu, u"Guardando...\nNo cierres el juego."},
    {"UI/cm/str/sav_de.s.z", 11, 0x35be96feu, u"Speichervorgang...\nDas Spiel nicht beenden."},
    {"UI/cm/str/sav_fr.s.z", 11, 0x801579e3u, u"Sauvegarde en cours...\nVeuillez ne pas quitter le jeu."},
    {"UI/cm/str/sav_it.s.z", 11, 0x4e247d1bu, u"Salvataggio in corso.\nNon chiudere il gioco."},
    {"UI/cm/str/sav_en.s.z", 13, 0x76f57a06u,
     u"Unable to read the data in this slot.\nRestart the game and try again."},
    {"UI/cm/str/sav_es.s.z", 13, 0x63705382u, u"Archivo mal cargado.\nReinicia el juego y\nvuelve a intentarlo."},
    {"UI/cm/str/sav_de.s.z", 13, 0xd4524660u,
     u"Fehler beim Laden.\nBitte das Spiel neu starten\nund erneut versuchen."},
    {"UI/cm/str/sav_fr.s.z", 13, 0x61005cd4u,
     u"Échec du chargement du fichier.\nVeuillez redémarrer le jeu et\nréessayer."},
    {"UI/cm/str/sav_it.s.z", 13, 0xe1fe5d20u, u"Impossibile leggere i dati nello slot.\nRiavvia il gioco\ne riprova."},
};

unsigned le32(const std::vector<std::uint8_t> &b, std::size_t at)
{
    return static_cast<unsigned>(b[at] | b[at + 1] << 8 | b[at + 2] << 16) | static_cast<unsigned>(b[at + 3]) << 24;
}

void put32(std::vector<std::uint8_t> &b, unsigned v)
{
    for (int i = 0; i < 4; ++i) {
        b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

// The string table's records (khdays::assets::load_string_table's format): the header's size and the
// count, then each record's length (itself included) and its UTF-16 text with its terminator.
bool read_table(const std::vector<std::uint8_t> &b, std::vector<std::u16string> &texts)
{
    if (b.size() < 8) {
        return false;
    }
    std::size_t at = le32(b, 0);
    const unsigned count = le32(b, 4);
    for (unsigned i = 0; i < count; ++i) {
        if (at + 4 > b.size()) {
            return false;
        }
        const std::size_t length = le32(b, at);
        if (length < 4 || at + length > b.size() || (length & 1) != 0) {
            return false;
        }
        std::u16string text;
        for (std::size_t c = at + 4; c + 2 <= at + length; c += 2) {
            text.push_back(static_cast<char16_t>(b[c] | b[c + 1] << 8));
        }
        while (!text.empty() && text.back() == u'\0') {
            text.pop_back();
        }
        texts.push_back(std::move(text));
        at += length;
    }
    return true;
}

std::vector<std::uint8_t> write_table(const std::vector<std::u16string> &texts)
{
    std::vector<std::uint8_t> b;
    put32(b, 8);
    put32(b, static_cast<unsigned>(texts.size()));
    for (const auto &text : texts) {
        put32(b, static_cast<unsigned>(4 + 2 * (text.size() + 1)));
        for (const char16_t c : text) {
            b.push_back(static_cast<std::uint8_t>(c));
            b.push_back(static_cast<std::uint8_t>(c >> 8));
        }
        b.push_back(0);
        b.push_back(0);
    }
    return b;
}

} // namespace

bool khdays_text_patch(const std::string &game_path, const std::filesystem::path &source,
                       std::vector<std::uint8_t> &out)
{
    const std::string path = !game_path.empty() && game_path[0] == '/' ? game_path.substr(1) : game_path;
    const Patch *first = nullptr;
    for (const auto &p : patches) {
        if (path == p.path) {
            first = &p;
            break;
        }
    }
    if (first == nullptr) {
        return false;
    }
    std::ifstream in(source, std::ios::binary);
    std::vector<std::uint8_t> packed{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<std::uint8_t> raw;
    try {
        raw = !packed.empty() && (packed[0] == 0x10 || packed[0] == 0x11) ? khdays::assets::lz_decompress(packed)
                                                                          : packed;
    } catch (const std::exception &) {
        return false; // not the game's format: served as it is
    }
    std::vector<std::u16string> texts;
    if (!read_table(raw, texts)) {
        return false;
    }
    bool changed = false;
    for (const auto &p : patches) {
        if (path == p.path && p.index < texts.size() && digest(texts[p.index]) == p.game) {
            texts[p.index] = p.port;
            changed = true;
        }
    }
    if (!changed) {
        return false;
    }
    out = khdays::assets::lz11_literals(write_table(texts));
    return true;
}

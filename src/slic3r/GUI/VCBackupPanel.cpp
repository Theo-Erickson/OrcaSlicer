// VCBackupPanel.cpp
// Drop into: src/slic3r/GUI/
// Add to src/slic3r/CMakeLists.txt under SLIC3R_GUI_SOURCES.

#include "VCBackupPanel.hpp"

#include <wx/msgdlg.h>
#include <wx/filename.h>
#include <wx/statline.h>
#include <wx/image.h>    // wxImage, wxPNGHandler
#include <wx/mstream.h>  // wxMemoryInputStream

#include "GUI_App.hpp" // wxGetApp()
#include "Plater.hpp"  // wxGetApp().plater()->load_project()
#include "I18N.hpp"    // _L()

// Boost iostreams ZIP — already a confirmed OrcaSlicer dependency.
// These headers are available via the boost dep in every OrcaSlicer build.
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/device/file.hpp>

// We use a raw ZIP parse via zlib's inflate rather than boost::iostreams zip,
// because boost::iostreams doesn't ship a zip *archive* filter by default.
// Instead we use the approach OrcaSlicer's own 3mf.cpp uses: open the file
// with the C standard library + a minimal ZIP local-file-header reader that
// only depends on zlib (already linked by OrcaSlicer for everything else).
//
// This avoids any minizip header dependency entirely.
// zlib.h is universally available in the OrcaSlicer build.
#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>
#include <cstdint>

namespace Slic3r { namespace GUI {

// ============================================================
//  ZIP parsing helpers (zlib only, no minizip headers needed)
// ============================================================
//
// A .3mf / .zip file has this structure:
//   [Local file header + data] * N
//   [Central directory] 
//   [End of central directory record]
//
// We parse the End-of-Central-Directory record to find the central
// directory, then walk it to find all Metadata/plate_N.png entries,
// pick the largest, seek to its local header, and inflate the data.
// This is the same logic minizip implements under the hood.

namespace {

// ZIP signatures
constexpr uint32_t ZIP_LOCAL_FILE_SIG  = 0x04034b50;
constexpr uint32_t ZIP_CENTRAL_DIR_SIG = 0x02014b50;
constexpr uint32_t ZIP_EOCD_SIG        = 0x06054b50;

// Read a little-endian uint16/uint32 from a byte buffer
inline uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
inline uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

struct ZipEntry {
    std::string  name;
    uint32_t     compressed_size{0};
    uint32_t     uncompressed_size{0};
    uint32_t     local_header_offset{0};
    uint16_t     compression_method{0}; // 0=stored, 8=deflate
};

// Find and parse the End-of-Central-Directory record.
// Returns false if not found (not a valid ZIP).
bool find_eocd(std::ifstream& f, uint32_t& cd_offset, uint32_t& cd_size,
               uint16_t& num_entries)
{
    f.seekg(0, std::ios::end);
    auto file_size = static_cast<int64_t>(f.tellg());
    if (file_size < 22) return false;

    // EOCD is at least 22 bytes; search backward from end (comment may follow)
    int64_t search_start = std::max<int64_t>(0, file_size - 65557);
    std::vector<uint8_t> buf(static_cast<size_t>(file_size - search_start));
    f.seekg(search_start);
    f.read(reinterpret_cast<char*>(buf.data()),
           static_cast<std::streamsize>(buf.size()));

    for (int64_t i = static_cast<int64_t>(buf.size()) - 22; i >= 0; --i) {
        if (read_u32(buf.data() + i) == ZIP_EOCD_SIG) {
            const uint8_t* p = buf.data() + i;
            num_entries = read_u16(p + 10);
            cd_size     = read_u32(p + 12);
            cd_offset   = read_u32(p + 16);
            return true;
        }
    }
    return false;
}

// Read all central-directory entries from the file.
std::vector<ZipEntry> read_central_directory(std::ifstream& f,
                                              uint32_t cd_offset,
                                              uint16_t num_entries)
{
    std::vector<ZipEntry> entries;
    f.seekg(cd_offset);

    for (uint16_t i = 0; i < num_entries; ++i) {
        uint8_t hdr[46];
        f.read(reinterpret_cast<char*>(hdr), 46);
        if (!f || read_u32(hdr) != ZIP_CENTRAL_DIR_SIG)
            break;

        uint16_t name_len    = read_u16(hdr + 28);
        uint16_t extra_len   = read_u16(hdr + 30);
        uint16_t comment_len = read_u16(hdr + 32);

        ZipEntry e;
        e.compression_method  = read_u16(hdr + 10);
        e.compressed_size     = read_u32(hdr + 20);
        e.uncompressed_size   = read_u32(hdr + 24);
        e.local_header_offset = read_u32(hdr + 42);

        std::string name(name_len, '\0');
        f.read(name.data(), name_len);
        e.name = std::move(name);

        f.seekg(extra_len + comment_len, std::ios::cur);
        entries.push_back(std::move(e));
    }
    return entries;
}

// Inflate (decompress) `compressed_size` bytes of deflate data from `f`
// into `out` (pre-sized to `uncompressed_size`). Returns true on success.
bool inflate_entry(std::ifstream& f,
                   uint32_t compressed_size,
                   std::vector<uint8_t>& out)
{
    std::vector<uint8_t> in(compressed_size);
    f.read(reinterpret_cast<char*>(in.data()),
           static_cast<std::streamsize>(compressed_size));
    if (!f) return false;

    z_stream zs{};
    zs.next_in   = in.data();
    zs.avail_in  = static_cast<uInt>(in.size());
    zs.next_out  = out.data();
    zs.avail_out = static_cast<uInt>(out.size());

    // inflateInit2 with -15 = raw deflate (no zlib header), which is what ZIP uses
    if (inflateInit2(&zs, -15) != Z_OK) return false;
    int ret = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    return (ret == Z_STREAM_END);
}

// Extract raw bytes for a specific ZipEntry from the already-open ifstream.
std::vector<uint8_t> extract_entry(std::ifstream& f, const ZipEntry& e)
{
    // Seek to local file header and skip it
    f.seekg(e.local_header_offset);
    uint8_t lhdr[30];
    f.read(reinterpret_cast<char*>(lhdr), 30);
    if (!f || read_u32(lhdr) != ZIP_LOCAL_FILE_SIG)
        return {};

    uint16_t name_len  = read_u16(lhdr + 26);
    uint16_t extra_len = read_u16(lhdr + 28);
    f.seekg(name_len + extra_len, std::ios::cur); // skip name + extra

    std::vector<uint8_t> out(e.uncompressed_size);

    if (e.compression_method == 0) {
        // Stored — no compression
        f.read(reinterpret_cast<char*>(out.data()),
               static_cast<std::streamsize>(e.uncompressed_size));
        if (!f) return {};
    } else if (e.compression_method == 8) {
        // Deflate
        if (!inflate_entry(f, e.compressed_size, out))
            return {};
    } else {
        return {}; // unsupported compression method
    }

    return out;
}

} // anonymous namespace

// ============================================================
//  Construction
// ============================================================

VCBackupPanel::VCBackupPanel(wxWindow* parent, ProjectVCBackupManager* manager, wxWindowID id) : wxPanel(parent, id), m_manager(manager)
{
    build_ui();
    // Start hidden; shown when a project row is selected
    Show(false);
}

// ============================================================
//  UI construction
// ============================================================

wxButton* VCBackupPanel::make_button(wxWindow* parent, const wxString& label, BtnStyle style)
{
    auto* btn = new wxButton(parent, wxID_ANY, label, wxDefaultPosition, wxSize(-1, 28));
    btn->SetWindowStyle(wxBORDER_NONE);

    wxColour bg, fg;
    switch (style) {
    case BtnStyle::Primary:
        bg = wxColour(26, 161, 121); // OrcaSlicer teal accent
        fg = wxColour(255, 255, 255);
        break;
    case BtnStyle::Danger:
        bg = wxColour(61, 26, 26);    // dark red surface
        fg = wxColour(240, 149, 149); // soft red text
        break;
    case BtnStyle::Secondary:
    default:
        bg = wxColour(55, 55, 55);    // dark neutral surface
        fg = wxColour(210, 210, 210); // light text
        break;
    }

    btn->SetBackgroundColour(bg);
    btn->SetForegroundColour(fg);
    btn->SetOwnBackgroundColour(bg);
    btn->SetOwnForegroundColour(fg);
    btn->Refresh();

    return btn;
}

void VCBackupPanel::build_ui()
{
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto* outer = new wxBoxSizer(wxVERTICAL);

    // ── Divider + header ─────────────────────────────────────
    outer->Add(new wxStaticLine(this), 0, wxEXPAND | wxTOP, 6);

    auto* header_row = new wxBoxSizer(wxHORIZONTAL);
    m_header_label   = new wxStaticText(this, wxID_ANY, _L("VCBackups"));
    wxFont f         = m_header_label->GetFont();
    f.SetWeight(wxFONTWEIGHT_BOLD);
    m_header_label->SetFont(f);
    m_header_label->SetForegroundColour(wxColour(200, 200, 200));
    header_row->Add(m_header_label, 1, wxALIGN_CENTER_VERTICAL);
    outer->Add(header_row, 0, wxEXPAND | wxALL, 8);

    // ── Empty-state label (shown when no VCBackups yet) ──────
    m_empty_label = new wxStaticText(this, wxID_ANY, _L("No VCBackups yet. Save the project to create one."));
    m_empty_label->SetForegroundColour(wxColour(130, 130, 130));
    outer->Add(m_empty_label, 0, wxLEFT | wxBOTTOM, 10);

    // ── Main content row: [list + buttons] | [divider] | [preview] ───────
    auto* content_row = new wxBoxSizer(wxHORIZONTAL);

    // ── Left column: list + button row ───────────────────────
    auto* left_col = new wxBoxSizer(wxVERTICAL);

    // ── VCBackup list ────────────────────────────────────────
    m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 140), wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
    m_list->SetBackgroundColour(wxColour(35, 35, 35));
    m_list->SetForegroundColour(wxColour(200, 200, 200));
    m_list->InsertColumn(0, _L("Saved"), wxLIST_FORMAT_LEFT, 160);
    m_list->InsertColumn(1, _L("Age"), wxLIST_FORMAT_LEFT, 110);
    m_list->InsertColumn(2, _L("VCBackup file"), wxLIST_FORMAT_LEFT, 280);
    left_col->Add(m_list, 1, wxEXPAND);

    // ── Button row ───────────────────────────────────────────
    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);

    m_btn_restore    = make_button(this, _L("Restore this VCBackup"), BtnStyle::Primary);
    m_btn_delete     = make_button(this, _L("Delete VCBackup"), BtnStyle::Secondary);
    m_btn_delete_all = make_button(this, _L("Delete all VCBackups"), BtnStyle::Danger);

    btn_row->Add(m_btn_restore, 0, wxRIGHT, 6);
    btn_row->Add(m_btn_delete, 0, wxRIGHT, 6);
    btn_row->AddStretchSpacer();
    btn_row->Add(m_btn_delete_all, 0);

    left_col->Add(btn_row, 0, wxEXPAND | wxTOP, 6);
    content_row->Add(left_col, 1, wxEXPAND | wxRIGHT, 10);

    // ── Vertical divider ─────────────────────────────────────
    content_row->Add(new wxStaticLine(this, wxID_ANY,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxLI_VERTICAL),
                     0, wxEXPAND | wxTOP | wxBOTTOM, 2);

    // ── Right column: thumbnail preview pane ─────────────────
    m_preview_pane = new wxPanel(this, wxID_ANY);
    m_preview_pane->SetBackgroundColour(wxColour(30, 30, 30));
    m_preview_pane->SetMinSize(wxSize(m_thumb_size.x + 16, -1));

    auto* preview_col = new wxBoxSizer(wxVERTICAL);
    preview_col->AddStretchSpacer();

    // Placeholder text (visible when nothing is selected)
    m_no_preview_lbl = new wxStaticText(m_preview_pane, wxID_ANY,
                                         _L("Select a VCBackup\nto preview"));
    m_no_preview_lbl->SetForegroundColour(wxColour(90, 90, 90));
    m_no_preview_lbl->SetWindowStyle(wxALIGN_CENTRE_HORIZONTAL);
    preview_col->Add(m_no_preview_lbl, 0, wxALIGN_CENTER | wxALL, 8);

    // Bitmap widget — starts with a 1×1 transparent placeholder
    wxBitmap placeholder(1, 1);
    placeholder.UseAlpha();
    m_preview_bmp = new wxStaticBitmap(m_preview_pane, wxID_ANY, placeholder);
    m_preview_bmp->Hide();
    preview_col->Add(m_preview_bmp, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, 8);

    // Caption label below the bitmap
    m_preview_label = new wxStaticText(m_preview_pane, wxID_ANY, wxEmptyString,
                                        wxDefaultPosition, wxDefaultSize,
                                        wxALIGN_CENTRE_HORIZONTAL);
    m_preview_label->SetForegroundColour(wxColour(140, 140, 140));
    wxFont lf = m_preview_label->GetFont();
    lf.SetPointSize(lf.GetPointSize() - 1);
    m_preview_label->SetFont(lf);
    m_preview_label->Hide();
    preview_col->Add(m_preview_label, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    preview_col->AddStretchSpacer();
    m_preview_pane->SetSizer(preview_col);
    content_row->Add(m_preview_pane, 0, wxEXPAND | wxLEFT, 10);

    outer->Add(content_row, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    SetSizer(outer);

    // ── Events ───────────────────────────────────────────────
    m_list->Bind(wxEVT_LIST_ITEM_SELECTED,   &VCBackupPanel::on_list_select,   this);
    m_list->Bind(wxEVT_LIST_ITEM_DESELECTED, &VCBackupPanel::on_list_deselect, this);
    m_btn_restore->Bind(wxEVT_BUTTON, &VCBackupPanel::on_restore, this);
    m_btn_delete->Bind(wxEVT_BUTTON, &VCBackupPanel::on_delete_VCBackup, this);
    m_btn_delete_all->Bind(wxEVT_BUTTON, &VCBackupPanel::on_delete_all, this);
}

// ============================================================
//  Public API
// ============================================================

void VCBackupPanel::load_project(const std::string& project_path)
{
    m_project_path = project_path;

    if (project_path.empty()) {
        Show(false);
        return;
    }

    // Update header
    wxFileName fn(wxString::FromUTF8(project_path));
    m_header_label->SetLabel(_L("VCBackups") + " — " + fn.GetFullName());

    refresh();
    show_placeholder(); // clear preview until user selects a row
    Show(true);

    // Force the parent to re-layout
    if (GetParent())
        GetParent()->Layout();
}

void VCBackupPanel::refresh()
{
    if (m_project_path.empty() || !m_manager)
        return;

    m_VCBackups = m_manager->list_VCBackups(m_project_path);
    populate_list();
}

// ============================================================
//  Thumbnail helpers
// ============================================================

wxBitmap VCBackupPanel::load_thumbnail(const std::string& VCBackup_path) const
{
    // Ensure the PNG image handler is registered (safe to call repeatedly)
    if (!wxImage::FindHandler(wxBITMAP_TYPE_PNG))
        wxImage::AddHandler(new wxPNGHandler);

    // ── Open the .3mf as a raw ZIP using only zlib + std::ifstream ────────
    std::ifstream f(VCBackup_path, std::ios::binary);
    if (!f.is_open())
        return wxBitmap();

    uint32_t cd_offset = 0, cd_size = 0;
    uint16_t num_entries = 0;
    if (!find_eocd(f, cd_offset, cd_size, num_entries))
        return wxBitmap(); // not a valid ZIP

    auto entries = read_central_directory(f, cd_offset, num_entries);

    // ── Find the largest Metadata/plate_N.png ────────────────────────────
    ZipEntry* best = nullptr;
    for (auto& e : entries) {
        bool is_plate_png =
            (e.name.rfind("Metadata/plate_", 0) == 0 ||
             e.name.rfind("metadata/plate_", 0) == 0) &&
            e.name.size() > 4 &&
            e.name.substr(e.name.size() - 4) == ".png";

        if (is_plate_png) {
            if (!best || e.uncompressed_size > best->uncompressed_size)
                best = &e;
        }
    }

    if (!best)
        return wxBitmap(); // no plate thumbnail in this .3mf

    // ── Extract and decode ────────────────────────────────────────────────
    std::vector<uint8_t> bytes = extract_entry(f, *best);
    if (bytes.empty())
        return wxBitmap();

    wxMemoryInputStream stream(bytes.data(), bytes.size());
    wxImage img;
    if (!img.LoadFile(stream, wxBITMAP_TYPE_PNG) || !img.IsOk())
        return wxBitmap();

    // Scale to fit m_thumb_size, never upscale
    int src_w = img.GetWidth();
    int src_h = img.GetHeight();
    if (src_w <= 0 || src_h <= 0)
        return wxBitmap();

    double scale = std::min(
        static_cast<double>(m_thumb_size.x) / src_w,
        static_cast<double>(m_thumb_size.y) / src_h);
    if (scale < 1.0)
        img = img.Scale(static_cast<int>(src_w * scale),
                        static_cast<int>(src_h * scale),
                        wxIMAGE_QUALITY_HIGH);

    return wxBitmap(img);
}

void VCBackupPanel::show_thumbnail(const std::string& VCBackup_path)
{
    wxBitmap bmp = load_thumbnail(VCBackup_path);

    if (!bmp.IsOk()) {
        // File exists but has no embedded plate thumbnail
        m_no_preview_lbl->SetLabel(_L("No preview\navailable"));
        m_no_preview_lbl->Show();
        m_preview_bmp->Hide();
        m_preview_label->Hide();
        m_preview_pane->Layout();
        return;
    }

    m_no_preview_lbl->Hide();
    m_preview_bmp->SetBitmap(bmp);
    m_preview_bmp->Show();

    wxFileName fn(wxString::FromUTF8(VCBackup_path));
    m_preview_label->SetLabel(fn.GetFullName());
    m_preview_label->Show();

    m_preview_pane->Layout();
    Layout();
}

void VCBackupPanel::show_placeholder() const
{
    m_no_preview_lbl->SetLabel(_L("Select a VCBackup\nto preview"));
    m_no_preview_lbl->Show();
    m_preview_bmp->Hide();
    m_preview_label->Hide();
    m_preview_pane->Layout();
}

// ============================================================
//  Private
// ============================================================

void VCBackupPanel::populate_list()
{
    m_list->DeleteAllItems();

    if (m_VCBackups.empty()) {
        m_empty_label->Show(true);
        m_list->Show(false);
        m_btn_restore->Enable(false);
        m_btn_delete->Enable(false);
        return;
    }

    m_empty_label->Show(false);
    m_list->Show(true);

    for (long i = 0; i < static_cast<long>(m_VCBackups.size()); ++i) {
        const auto& snap = m_VCBackups[i];
        long        idx  = m_list->InsertItem(i, wxString::FromUTF8(snap.date_string()));
        m_list->SetItem(idx, 1, wxString::FromUTF8(snap.relative_age()));

        // Show just the filename in the list, not the full path
        wxFileName fn(wxString::FromUTF8(snap.VCBackup_path));
        m_list->SetItem(idx, 2, fn.GetFullName());
    }

    // Nothing selected yet — disable restore/delete until user picks one
    m_btn_restore->Enable(false);
    m_btn_delete->Enable(false);

    Layout();
}

void VCBackupPanel::on_list_select(wxListEvent& evt)
{
    long idx   = evt.GetIndex();
    bool valid = (idx >= 0 && static_cast<size_t>(idx) < m_VCBackups.size());
    m_btn_restore->Enable(valid);
    m_btn_delete->Enable(valid);

    if (valid)
        show_thumbnail(m_VCBackups[idx].VCBackup_path);
    else
        show_placeholder();
}

void VCBackupPanel::on_list_deselect(wxListEvent& /*evt*/)
{
    m_btn_restore->Enable(false);
    m_btn_delete->Enable(false);
    show_placeholder();
}

void VCBackupPanel::on_restore(wxCommandEvent& /*evt*/)
{
    long sel = m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= m_VCBackups.size())
        return;

    const VCBackup& snap = m_VCBackups[sel];

    int confirm = wxMessageBox(wxString::Format(_L("Restore VCBackup from %s?\n\n"
                                                   "The current project file will be backed up as .bak before overwriting."),
                                                wxString::FromUTF8(snap.date_string())),
                               _L("Restore VCBackup"), wxYES_NO | wxICON_QUESTION, this);

    if (confirm != wxYES)
        return;

    bool ok = m_manager->restore(snap.VCBackup_path, m_project_path);
    if (!ok) {
        wxMessageBox(_L("Restore failed. Check that the VCBackup file still exists."), _L("Error"), wxOK | wxICON_ERROR, this);
        return;
    }

    // Reload the restored project in the Plater
    wxGetApp().plater()->load_project(wxString::FromUTF8(m_project_path));
}

void VCBackupPanel::on_delete_VCBackup(wxCommandEvent& /*evt*/)
{
    long sel = m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= m_VCBackups.size())
        return;

    const VCBackup& VCBack = m_VCBackups[sel];

    int confirm = wxMessageBox(wxString::Format(_L("Delete VCBackup from %s?"), wxString::FromUTF8(VCBack.date_string())),
                               _L("Delete VCBackup"), wxYES_NO | wxICON_QUESTION, this);

    if (confirm != wxYES)
        return;

    m_manager->delete_VCBackup(VCBack.VCBackup_path);
    show_placeholder();
    refresh();
}

void VCBackupPanel::on_delete_all(wxCommandEvent& /*evt*/)
{
    if (m_VCBackups.empty())
        return;

    wxFileName fn(wxString::FromUTF8(m_project_path));
    int        confirm = wxMessageBox(wxString::Format(_L("Delete all %zu VCBackups for \"%s\"?"), m_VCBackups.size(),
                                                       fn.GetFullName().ToUTF8().data()),
                                      _L("Delete all VCBackups"), wxYES_NO | wxICON_WARNING, this);

    if (confirm != wxYES)
        return;

    m_manager->delete_all_VCBackups(m_project_path);
    show_placeholder();
    refresh();
}

}} // namespace Slic3r::GUI
#pragma once
#include <wx/dialog.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/checkbox.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/statbmp.h>
#include "Widgets/Button.hpp"
#include "GUI_App.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"

namespace Slic3r {
namespace GUI {

enum class SeparatorType {
    DISC = 0,
    BOUNDING_BOX = 1,
    OBJECT_SILHOUETTE = 2,
    PERIMETER_RING = 3
};

class StackObjectsDialog : public wxDialog
{
public:
    StackObjectsDialog(wxWindow* parent, const ModelObject* src_obj)
        : wxDialog(parent, wxID_ANY, _L("Stack Objects"),
                   wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          m_src_obj(src_obj)
    {
        // ── Determine default support state ───────────────────────────
        bool default_supports = false;
        if (src_obj && src_obj->config.has("enable_support")) {
            default_supports = src_obj->config.option("enable_support")->getBool();
        } else {
            const DynamicPrintConfig& print_cfg =
                wxGetApp().preset_bundle->prints.get_edited_preset().config;
            if (const ConfigOptionBool* opt =
                    print_cfg.option<ConfigOptionBool>("enable_support"))
                default_supports = opt->value;
        }

        wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

        // ── Preview area ──────────────────────────────────────────────
        auto* preview_panel = new wxPanel(this);
        preview_panel->SetBackgroundColour(wxColour(45, 45, 48));
        auto* preview_sizer = new wxBoxSizer(wxVERTICAL);
        m_preview_bitmap = new wxStaticBitmap(preview_panel, wxID_ANY,
                                              wxNullBitmap,
                                              wxDefaultPosition,
                                              wxSize(300, 220));
        preview_sizer->Add(m_preview_bitmap, 1, wxALL | wxEXPAND, 10);
        preview_panel->SetSizer(preview_sizer);
        main_sizer->Add(preview_panel, 0, wxEXPAND | wxALL, 0);

        // ── Core settings grid ────────────────────────────────────────
        wxFlexGridSizer* grid = new wxFlexGridSizer(2, 2, 10, 16);
        grid->AddGrowableCol(1);

        // Number of copies
        grid->Add(new wxStaticText(this, wxID_ANY, _L("Number of copies:")),
                  0, wxALIGN_CENTER_VERTICAL);
        wxBoxSizer* copies_row = new wxBoxSizer(wxHORIZONTAL);
        m_copies = new wxSpinCtrl(this, wxID_ANY, "3",
                                  wxDefaultPosition, wxSize(80, -1),
                                  wxSP_ARROW_KEYS, 1, 100, 3);
        m_copies->SetToolTip(
            _L("How many copies of the object to stack on top of each other."));
        m_copies->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { UpdatePreview(); });
        copies_row->Add(m_copies, 0, wxRIGHT, 8);

        auto* btn_max = new wxButton(this, wxID_ANY, _L("Max"),
                                     wxDefaultPosition, wxSize(50, -1));
        btn_max->SetToolTip(
            _L("Calculate the maximum number of copies that fit within "
               "the build volume height, based on current settings."));
        btn_max->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CalculateMaxCopies(); });
        copies_row->Add(btn_max, 0);
        grid->Add(copies_row, 0, wxEXPAND);

        // Separator thickness
        auto* sep_thick_label = new wxStaticText(this, wxID_ANY,
            _L("Separator thickness (layers):"));
        sep_thick_label->SetToolTip(
            _L("How many layers thick the breakaway separator disc is. "
               "Thicker separators are more rigid and easier to handle "
               "but use more material. 3-5 layers is typically sufficient."));
        grid->Add(sep_thick_label, 0, wxALIGN_CENTER_VERTICAL);
        m_separator_layers = new wxSpinCtrl(this, wxID_ANY, "3",
                                            wxDefaultPosition, wxSize(80, -1),
                                            wxSP_ARROW_KEYS, 1, 20, 3);
        m_separator_layers->SetToolTip(
            _L("How many layers thick the breakaway separator disc is. "
               "Thicker separators are more rigid and easier to handle "
               "but use more material. 3-5 layers is typically sufficient."));
        m_separator_layers->Bind(wxEVT_SPINCTRL,
            [this](wxSpinEvent&) { UpdatePreview(); });
        grid->Add(m_separator_layers, 0, wxEXPAND);

        // Breakaway gap
        auto* gap_label = new wxStaticText(this, wxID_ANY,
            _L("Breakaway gap (layers):"));
        gap_label->SetToolTip(
            _L("How many layers of space to leave between the separator disc "
               "and the object above it. This gap is filled with support "
               "interface material that snaps off cleanly. More layers means "
               "easier separation but a taller stack. 4-6 layers recommended."));
        grid->Add(gap_label, 0, wxALIGN_CENTER_VERTICAL);
        m_gap_layers = new wxSpinCtrl(this, wxID_ANY, "4",
                                      wxDefaultPosition, wxSize(80, -1),
                                      wxSP_ARROW_KEYS, 2, 20, 4);
        m_gap_layers->SetToolTip(
            _L("How many layers of space to leave between the separator disc "
               "and the object above it. This gap is filled with support "
               "interface material that snaps off cleanly. More layers means "
               "easier separation but a taller stack. 4-6 layers recommended."));
        m_gap_layers->Bind(wxEVT_SPINCTRL,
            [this](wxSpinEvent&) { UpdatePreview(); });
        grid->Add(m_gap_layers, 0, wxEXPAND);

        // Separator size ratio
        auto* sep_size_label = new wxStaticText(this, wxID_ANY,
            _L("Separator size (x object):"));
        sep_size_label->SetToolTip(
            _L("How wide the separator disc is relative to the object footprint. "
               "1.0 matches the object exactly. Larger values give the disc more "
               "surface area for support to attach to, improving stability. "
               "1.1 to 1.5 is a good range."));
        grid->Add(sep_size_label, 0, wxALIGN_CENTER_VERTICAL);
        m_separator_to_object_size_ratio = new wxSpinCtrlDouble(
            this, wxID_ANY, "1.25",
            wxDefaultPosition, wxSize(80, -1),
            wxSP_ARROW_KEYS, 0.5, 3.0, 1.25, 0.05);
        m_separator_to_object_size_ratio->SetDigits(2);
        m_separator_to_object_size_ratio->SetToolTip(
            _L("How wide the separator disc is relative to the object footprint. "
               "1.0 matches the object exactly. Larger values give the disc more "
               "surface area for support to attach to, improving stability. "
               "1.1 to 1.5 is a good range."));
        m_separator_to_object_size_ratio->Bind(wxEVT_SPINCTRLDOUBLE,
            [this](wxSpinDoubleEvent&) { UpdatePreview(); });
        grid->Add(m_separator_to_object_size_ratio, 0, wxEXPAND);

        main_sizer->Add(grid, 0, wxEXPAND | wxALL, 16);

        // ── Base separator section ────────────────────────────────────
        main_sizer->Add(new wxStaticLine(this), 0,
                        wxEXPAND | wxLEFT | wxRIGHT, 16);

        wxFont bold_font = GetFont();
        bold_font.SetWeight(wxFONTWEIGHT_BOLD);

        auto* base_label = new wxStaticText(this, wxID_ANY,
                                            _L("Base separator:"));
        base_label->SetFont(bold_font);
        main_sizer->Add(base_label, 0, wxLEFT | wxTOP, 16);

        wxBoxSizer* base_sep_col = new wxBoxSizer(wxVERTICAL);

        m_use_base_separator = new wxCheckBox(this, wxID_ANY,
            _L("Add base separator on build plate"));
        m_use_base_separator->SetValue(true);
        m_use_base_separator->SetToolTip(
            _L("When checked, a separator disc is placed on the build plate "
               "directly below the first object. This lets you peel the base "
               "object off the build plate cleanly, just like the copies above "
               "it. The base separator is larger than the others for better "
               "bed adhesion."));
        m_use_base_separator->Bind(wxEVT_CHECKBOX,
            [this](wxCommandEvent&) { UpdateBaseSepControls(); UpdatePreview(); });
        base_sep_col->Add(m_use_base_separator, 0);

        wxBoxSizer* base_size_row = new wxBoxSizer(wxHORIZONTAL);
        m_base_size_label = new wxStaticText(this, wxID_ANY,
            _L("Base disc size (x object):"));
        m_base_size_label->SetToolTip(
            _L("How wide the base separator is relative to the object footprint. "
               "Should be larger than the regular separator size to improve "
               "first-layer adhesion and overall stack stability. "
               "1.5 to 2.0 recommended."));
        base_size_row->Add(m_base_size_label, 0,
                           wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        m_first_separator_size_ratio = new wxSpinCtrlDouble(
            this, wxID_ANY, "1.50",
            wxDefaultPosition, wxSize(80, -1),
            wxSP_ARROW_KEYS, 1.0, 3.0, 1.50, 0.05);
        m_first_separator_size_ratio->SetDigits(2);
        m_first_separator_size_ratio->SetToolTip(
            _L("How wide the base separator is relative to the object footprint. "
               "Should be larger than the regular separator size to improve "
               "first-layer adhesion and overall stack stability. "
               "1.5 to 2.0 recommended."));
        m_first_separator_size_ratio->Bind(wxEVT_SPINCTRLDOUBLE,
            [this](wxSpinDoubleEvent&) { UpdatePreview(); });
        base_size_row->Add(m_first_separator_size_ratio, 0);
        base_sep_col->Add(base_size_row, 0, wxLEFT | wxTOP, 20);

        main_sizer->Add(base_sep_col, 0, wxLEFT | wxTOP, 16);

        // ── Separator type section ────────────────────────────────────
        main_sizer->Add(new wxStaticLine(this), 0,
                        wxEXPAND | wxLEFT | wxRIGHT, 16);

        auto* sep_type_label = new wxStaticText(this, wxID_ANY,
                                                _L("Separator type:"));
        sep_type_label->SetFont(bold_font);
        main_sizer->Add(sep_type_label, 0, wxLEFT | wxTOP, 16);

        auto add_separator_option = [&](const wxString& label,
                                        const wxString& hint,
                                        SeparatorType   type,
                                        bool            is_first) -> wxRadioButton* {
            wxBoxSizer* row = new wxBoxSizer(wxVERTICAL);
            auto* rb = new wxRadioButton(this, wxID_ANY, label,
                                         wxDefaultPosition, wxDefaultSize,
                                         is_first ? wxRB_GROUP : 0);
            rb->SetValue(is_first);
            if (is_first)
                m_separator_type = type;
            rb->SetClientData((void*)(intptr_t)type);
            rb->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent& evt) {
                wxRadioButton* btn = dynamic_cast<wxRadioButton*>(
                    evt.GetEventObject());
                if (btn) {
                    m_separator_type =
                        (SeparatorType)(intptr_t)btn->GetClientData();
                    UpdatePreview();
                }
            });
            row->Add(rb, 0);
            if (!hint.IsEmpty()) {
                auto* sub = new wxStaticText(this, wxID_ANY, hint);
                sub->SetForegroundColour(wxColour(120, 120, 120));
                wxFont smallF = sub->GetFont();
                smallF.SetPointSize(smallF.GetPointSize() - 1);
                sub->SetFont(smallF);
                row->Add(sub, 0, wxLEFT, 20);
            }
            main_sizer->Add(row, 0, wxLEFT | wxTOP, 16);
            return rb;
        };

        m_rb_disc = add_separator_option(
            _L("Disc"),
            _L("Circular disc. Simple, uses least material, works for most objects."),
            SeparatorType::DISC, true);

        m_rb_bbox = add_separator_option(
            _L("Bounding box"),
            _L("Rectangle matching the object footprint. Good for boxy objects."),
            SeparatorType::BOUNDING_BOX, false);

        m_rb_silhouette = add_separator_option(
            _L("Object silhouette"),
            _L("Flattened copy of the object shape. Best fit, uses more material."),
            SeparatorType::OBJECT_SILHOUETTE, false);

        m_rb_perimeter = add_separator_option(
            _L("Perimeter ring"),
            _L("Hollow ring outline. Less material, may be less stable."),
            SeparatorType::PERIMETER_RING, false);

        // ── Support section ───────────────────────────────────────────
        main_sizer->Add(new wxStaticLine(this), 0,
                        wxEXPAND | wxLEFT | wxRIGHT, 16);

        auto* support_label = new wxStaticText(this, wxID_ANY, _L("Support:"));
        support_label->SetFont(bold_font);
        main_sizer->Add(support_label, 0, wxLEFT | wxTOP, 16);

        wxBoxSizer* support_col = new wxBoxSizer(wxVERTICAL);
        m_support_objects = new wxCheckBox(this, wxID_ANY,
            _L("Support objects"));
        m_support_objects->SetValue(default_supports);
        m_support_objects->SetToolTip(
            _L("When checked, support material is generated under the objects "
               "themselves for overhangs. When unchecked, support blockers "
               "prevent support from generating inside the object body: only "
               "the breakaway gap zones get support. Uncheck for faster "
               "slicing and easier support removal."));
        m_support_objects->Bind(wxEVT_CHECKBOX,
            [this](wxCommandEvent&) { UpdatePreview(); });
        support_col->Add(m_support_objects, 0);

        auto* support_hint = new wxStaticText(this, wxID_ANY,
            _L("When unchecked, only the breakaway gap zones receive support.\n"
               "Check this if your object has significant overhangs."));
        support_hint->SetForegroundColour(wxColour(120, 120, 120));
        wxFont smallF = support_hint->GetFont();
        smallF.SetPointSize(smallF.GetPointSize() - 1);
        support_hint->SetFont(smallF);
        support_col->Add(support_hint, 0, wxLEFT, 20);
        main_sizer->Add(support_col, 0, wxLEFT | wxTOP, 16);

        // ── Info text ─────────────────────────────────────────────────
        main_sizer->Add(new wxStaticLine(this), 0,
                        wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 16);

        auto* info_text = new wxStaticText(this, wxID_ANY,
            _L("Objects are stacked with a thin separator disc between each copy.\n"
               "A breakaway gap above each disc is filled with support interface\n"
               "material so each copy can be peeled off individually after printing.\n"
               "Tree supports hold the discs up between objects."));
        info_text->SetForegroundColour(wxColour(120, 120, 120));
        main_sizer->Add(info_text, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 16);

        // ── Buttons ───────────────────────────────────────────────────
        main_sizer->Add(new wxStaticLine(this), 0, wxEXPAND);
        wxBoxSizer* btn_sizer = new wxBoxSizer(wxHORIZONTAL);

        auto* btn_fill_bed = new wxButton(this, wxID_ANY, _L("Fill Bed"));
        btn_fill_bed->SetToolTip(
            _L("Stack objects and then arrange multiple stacks across "
               "the build plate to fill available space."));
        btn_fill_bed->Bind(wxEVT_BUTTON,
            [this](wxCommandEvent&) { FillBed(); });
        btn_sizer->Add(btn_fill_bed, 0, wxRIGHT, 8);

        btn_sizer->AddStretchSpacer();

        btn_sizer->Add(new wxButton(this, wxID_CANCEL, _L("Cancel")),
                       0, wxRIGHT, 8);
        auto* btn_ok = new wxButton(this, wxID_OK, _L("Stack"));
        btn_ok->SetDefault();
        btn_sizer->Add(btn_ok, 0);
        main_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, 12);

        SetSizerAndFit(main_sizer);
        CentreOnParent();
        wxGetApp().UpdateDlgDarkUI(this);
        UpdateBaseSepControls();
        UpdatePreview();
    }

    // ── Accessors ─────────────────────────────────────────────────────
    int  get_copies()           const { return m_copies->GetValue(); }
    int  get_separator_layers() const { return m_separator_layers->GetValue(); }
    int  get_gap_layers()       const { return m_gap_layers->GetValue(); }
    bool get_support_objects()  const { return m_support_objects->GetValue(); }
    bool get_use_base_separator() const { return m_use_base_separator->GetValue(); }
    bool should_fill_bed()      const { return m_fill_bed_requested; }

    float get_separator_to_object_size_ratio() const {
        double val;
        if (m_separator_to_object_size_ratio->GetTextValue().ToDouble(&val))
            return (float)val;
        return 1.25f;
    }

    float get_first_separator_size_ratio() const {
        if (!m_use_base_separator->GetValue())
            return get_separator_to_object_size_ratio();
        double val;
        if (m_first_separator_size_ratio->GetTextValue().ToDouble(&val))
            return (float)val;
        return 1.5f;
    }

    SeparatorType get_separator_type() const {
        if (m_rb_disc->GetValue())       return SeparatorType::DISC;
        if (m_rb_bbox->GetValue())       return SeparatorType::BOUNDING_BOX;
        if (m_rb_silhouette->GetValue()) return SeparatorType::OBJECT_SILHOUETTE;
        if (m_rb_perimeter->GetValue())  return SeparatorType::PERIMETER_RING;
        return SeparatorType::DISC;
    }

private:
    void UpdateBaseSepControls() {
        const bool enabled = m_use_base_separator->GetValue();
        m_base_size_label->Enable(enabled);
        m_first_separator_size_ratio->Enable(enabled);
    }

    void CalculateMaxCopies() {
        const DynamicPrintConfig& printer_cfg =
            wxGetApp().preset_bundle->printers.get_edited_preset().config;
        const ConfigOptionFloats* height_opt =
            printer_cfg.option<ConfigOptionFloats>("printable_height");
        if (!height_opt || height_opt->values.empty()) {
            wxMessageBox(_L("Could not determine build volume height."),
                         _L("Error"), wxOK | wxICON_ERROR);
            return;
        }
        const double build_height = height_opt->values[0];

        if (!m_src_obj) return;
        BoundingBoxf3 obj_bb;
        for (const ModelVolume* mv : m_src_obj->volumes)
            obj_bb.merge(mv->mesh().bounding_box());
        const double obj_height = obj_bb.max.z() - obj_bb.min.z();

        const double layer_height =
            wxGetApp().preset_bundle->prints
                .get_edited_preset().config.opt_float("layer_height");

        const double sep_thickness = layer_height * m_separator_layers->GetValue();
        const double interface_gap = layer_height * m_gap_layers->GetValue();

        // Matches the step formula in do_stack_objects exactly:
        // [object] [gap] [disc] [gap] per copy
        const double step = obj_height + interface_gap + sep_thickness + interface_gap;

        // Base lift if base separator is enabled
        const double base_lift = m_use_base_separator->GetValue()
                                 ? (sep_thickness + interface_gap) : 0.0;

        const double available = build_height - obj_height - base_lift;
        const int max_copies   = std::max(1, (int)(available / step));

        m_copies->SetValue(max_copies);
        UpdatePreview();

        wxMessageBox(
            wxString::Format(
                _L("Maximum copies: %d\n"
                   "Build height: %.1f mm\n"
                   "Object height: %.1f mm\n"
                   "Height per copy including separators: %.1f mm\n"
                   "Base separator lift: %.1f mm"),
                max_copies, build_height, obj_height, step, base_lift),
            _L("Max Copies"), wxOK | wxICON_INFORMATION);
    }

    void FillBed() {
        m_fill_bed_requested = true;
        EndModal(wxID_OK);
    }

    void UpdatePreview() {
        const int  w         = 300;
        const int  h         = 220;
        wxBitmap   bmp(w, h);
        wxMemoryDC dc(bmp);

        dc.SetBackground(wxBrush(wxColour(45, 45, 48)));
        dc.Clear();

        const int  copies    = m_copies->GetValue();
        const bool has_sup   = m_support_objects->GetValue();
        const bool has_base  = m_use_base_separator->GetValue();

        // Scale elements to fit all copies + separators in the preview height.
        // Layout per level bottom-up:
        //   [base sep (optional)] [gap] [object] [gap] [sep] [gap] [object] ...
        const int total_objs  = copies + 1;
        const int usable_h    = h - 20;
        const int obj_h       = std::max(8,  usable_h / (total_objs * 3));
        const int sep_h       = std::max(3,  obj_h / 3);
        const int gap_h       = std::max(2,  obj_h / 5);
        const int obj_w       = 70;
        const int cx          = w / 2;

        // Drawing proceeds bottom-up: y starts at the bottom of the canvas.
        int y = h - 10;

        auto draw_rect = [&](int x, int width, int height, wxColour col) {
            dc.SetBrush(wxBrush(col));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(x, y - height, width, height);
            y -= height;
        };

        auto draw_sep = [&](double ratio) {
            int sep_w = (int)(obj_w * ratio);
            int sep_x = cx - sep_w / 2;
            dc.SetPen(*wxTRANSPARENT_PEN);
            switch (get_separator_type()) {
                case SeparatorType::DISC:
                    dc.SetBrush(wxBrush(wxColour(100, 150, 255)));
                    dc.DrawRoundedRectangle(sep_x, y - sep_h, sep_w, sep_h, 2);
                    break;
                case SeparatorType::BOUNDING_BOX:
                    dc.SetBrush(wxBrush(wxColour(100, 150, 255)));
                    dc.DrawRectangle(sep_x, y - sep_h, sep_w, sep_h);
                    break;
                case SeparatorType::OBJECT_SILHOUETTE:
                    dc.SetBrush(wxBrush(wxColour(100, 150, 255)));
                    dc.DrawRectangle(cx - obj_w / 2, y - sep_h, obj_w, sep_h);
                    break;
                case SeparatorType::PERIMETER_RING:
                    dc.SetBrush(*wxTRANSPARENT_BRUSH);
                    dc.SetPen(wxPen(wxColour(100, 150, 255), 2));
                    dc.DrawRoundedRectangle(sep_x, y - sep_h, sep_w, sep_h, 2);
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    break;
            }
            y -= sep_h;
        };

        const double sep_ratio  = m_separator_to_object_size_ratio->GetValue();
        const double base_ratio = m_first_separator_size_ratio->GetValue();

        // Bottom: optional base separator + gap above it
        if (has_base) {
            draw_sep(base_ratio);
            draw_rect(cx - (int)(obj_w * base_ratio) / 2,
                      (int)(obj_w * base_ratio),
                      gap_h, wxColour(255, 200, 80));
        }

        // Base object
        draw_rect(cx - obj_w / 2, obj_w, obj_h, wxColour(200, 200, 200));

        // Each copy: gap above object, separator, breakaway gap, next object
        for (int i = 0; i < copies; ++i) {
            // Gap above the object below (tree support zone)
            draw_rect(cx - obj_w / 2, obj_w, gap_h, wxColour(80, 200, 80));
            // Separator disc
            draw_sep(sep_ratio);
            // Breakaway gap above disc (support interface zone)
            draw_rect(cx - (int)(obj_w * sep_ratio) / 2,
                      (int)(obj_w * sep_ratio),
                      gap_h, wxColour(255, 200, 80));
            // Next object
            draw_rect(cx - obj_w / 2, obj_w, obj_h, wxColour(200, 200, 200));
        }

        // Legend
        dc.SetTextForeground(wxColour(180, 180, 180));
        wxFont font = dc.GetFont();
        font.SetPointSize(7);
        dc.SetFont(font);

        int ly = 8;
        auto draw_legend = [&](wxColour col, const wxString& label) {
            dc.SetBrush(wxBrush(col));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(8, ly, 10, 10);
            dc.DrawText(label, 22, ly);
            ly += 14;
        };

        draw_legend(wxColour(200, 200, 200), _L("Object"));
        draw_legend(wxColour(100, 150, 255), _L("Separator disc"));
        draw_legend(wxColour(255, 200, 80),  _L("Breakaway gap (interface)"));
        draw_legend(wxColour(80,  200, 80),  _L("Support gap"));
        if (has_sup)
            draw_legend(wxColour(80, 200, 80), _L("Object support"));

        dc.SelectObject(wxNullBitmap);
        m_preview_bitmap->SetBitmap(bmp);
    }

    // ── Members ───────────────────────────────────────────────────────
    const ModelObject*    m_src_obj;
    wxSpinCtrl*           m_copies;
    wxSpinCtrl*           m_separator_layers;
    wxSpinCtrl*           m_gap_layers;
    wxSpinCtrlDouble*     m_separator_to_object_size_ratio;
    wxSpinCtrlDouble*     m_first_separator_size_ratio;
    wxStaticText*         m_base_size_label;
    wxCheckBox*           m_use_base_separator;
    wxCheckBox*           m_support_objects;
    wxRadioButton*        m_rb_disc;
    wxRadioButton*        m_rb_bbox;
    wxRadioButton*        m_rb_silhouette;
    wxRadioButton*        m_rb_perimeter;
    wxStaticBitmap*       m_preview_bitmap;
    SeparatorType         m_separator_type  = SeparatorType::DISC;
    bool                  m_fill_bed_requested = false;
};

} // namespace GUI
} // namespace Slic3r
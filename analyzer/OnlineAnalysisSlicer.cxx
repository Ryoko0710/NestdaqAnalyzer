#include <fairmq/Device.h>
#include <fairmq/runDevice.h>

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <sstream>

// ROOT Headers
#include "TSystem.h"
#include "TApplication.h"
#include "TROOT.h"
#include "TH1.h"
#include "TH2.h"
#include "TCanvas.h"
#include "THttpServer.h"

// Project Headers
#include "TFSlicerUnpacker.h"
#include "UnpackTdc.h"
#include "KTimer.cxx" 

namespace bpo = boost::program_options;

struct OnlineAnalysisTrigger : fair::mq::Device
{
    struct OptionKey {
        static constexpr std::string_view InputChannelName {"in-chan-name"};
    };

    OnlineAnalysisTrigger()
    {
    }

    void InitTask() override
    {
        using opt = OptionKey;
        fInputChannelName = fConfig->GetValue<std::string>(opt::InputChannelName.data());
        LOG(info) << "InitTask: Input Channel = " << fInputChannelName;

        // Initialize ROOT Application
        gROOT->SetBatch(kTRUE);
        if (!gApplication) {
            static int argc = 1;
            static char* argv[] = {(char*)"OnlineAnalysisTrigger", nullptr};
            new TApplication("OnlineAnalysisTrigger", &argc, argv);
        }

        // Initialize HTTP Server
        if (!fServer) {
            fServer = new THttpServer("http:8889"); 
            fServer->SetReadOnly(kTRUE);
            LOG(info) << "HTTP Server started on port 8889";
        }

        // Initialize Global Histograms
        // Trigger interval: 0-25 us
        fH1TrigInterval = new TH1F("h1_trig_interval", "Trigger Interval;Time [us];Counts", 1000, 0, 25);  
        fH1TrigTime     = new TH1F("h1_trig_time", "Trigger Time Distribution;Time [s];Counts", 1000, 0, 10e-6); 

        // Summary Canvas
        fCanvasSummary = new TCanvas("c_summary", "Summary", 800, 800);
        fCanvasSummary->Divide(1, 2);
        fCanvasSummary->cd(1)->SetLogy(); // LogY for Interval
        
        if (fServer) {
            fServer->Register("/Summary", fH1TrigInterval);
            fServer->Register("/Summary", fH1TrigTime);
            fServer->Register("/Summary", fCanvasSummary);
            
            fCanvasSummary->cd(1); fH1TrigInterval->Draw();
            fCanvasSummary->cd(2); fH1TrigTime->Draw();

            fServer->SetItemField("/", "_monitoring", "1000");
            fServer->SetItemField("/Summary", "_monitoring", "1000");
        }

        fDrawTimer.SetDuration(100); 
    }

    bool ConditionalRun() override
    {
        FairMQParts parts;
        if (Receive(parts, fInputChannelName, 0, 100) > 0) {
            // Processing
            for (auto& msg : parts) {
                if (msg->GetSize() < sizeof(uint64_t)) continue;
                uint64_t magic = *reinterpret_cast<uint64_t*>(msg->GetData());
                
                // DEBUG LOG
                if (fMsgCount % 1000 == 0) {
                    LOG(info) << "Received MsgSize: " << msg->GetSize() << " Magic: 0x" << std::hex << magic << std::dec;
                }
                fMsgCount++;

                if (magic == TimeFrame::MAGIC) {
                     ProcessTimeFrame(*msg);
                } else if (fMsgCount < 10) {
                     LOG(warn) << "Unknown Magic in OnlineAnalysisSlicer: 0x" << std::hex << magic << std::dec;
                }
            }
        }

        if (fDrawTimer.Check()) {
            UpdateDisplay();
        }

        return true;
    }

    void ProcessTimeFrame(fair::mq::Message& msg)
    {
        uint64_t* data = reinterpret_cast<uint64_t*>(msg.GetData());
        size_t size = msg.GetSize();

        fUnpacker.set_data(data, size);
        fUnpacker.unpack();
        
        size_t num_slices = fUnpacker.get_num_slices();
        if (fMsgCount < 10 || num_slices > 0) {
            LOG(info) << "ProcessTimeFrame - MsgSize: " << size << " NumSlices: " << num_slices;
        }

        for (size_t i = 0; i < num_slices; ++i) {
            const auto& slice = fUnpacker.get_slice(i);
            
            // Check if this is a valid TDC Trigger Slice
            // We want to process PHYSICS TRIGGERS (Type 0x10) to see TDC peaks.
            // Heartbeats (0xAA...) contain uncorrelated noise which flattens the distribution.
            // Header (0x45...) should be skipped.
            
            bool is_physics = (slice.trigger_info.type & 0xFFFF) == 0x10; // Check standard low bits
            // Or simple exact match if masking upper bits?
            // Log showed "Type: 16".
            
            if (!is_physics) {
                // Skip Heartbeats and Headers for histograms to avoid washing out peaks
                continue; 
            }
            
            // 1. Process Trigger Info
            uint64_t trg_time = slice.trigger_info.time & 0xFFFFFFFFFFFF; 
            
            if (fFirstTrigTime == 0) fFirstTrigTime = trg_time;

            // Trigger Interval
            if (fLastTrigTime > 0 && trg_time > fLastTrigTime) {
                 double interval_ns = static_cast<double>(trg_time - fLastTrigTime); 
                 fH1TrigInterval->Fill(interval_ns * 1e-3); // ns -> us
            }
            fLastTrigTime = trg_time;
            
            // Trigger Time (Relative to first trigger)
            if (trg_time >= fFirstTrigTime) {
                double rel_time_sec = (trg_time - fFirstTrigTime) * 1e-9;
                fH1TrigTime->Fill(rel_time_sec);
                if (i == 0) { // Log once per TF to avoid flood
                    LOG(info) << "TrgTime: " << trg_time << " First: " << fFirstTrigTime << " Rel: " << rel_time_sec;
                }
            }

            // 2. Process Hits (per FEM)
            for (auto const& [fem_id, tdc_data] : slice.data_by_fem) {
                int max_ch = 0;
                int multiplicity = 0;
                if (slice.fem_types.find(fem_id) == slice.fem_types.end()) continue;
                int fem_type = slice.fem_types.at(fem_id);
                bool is_hr = (fem_type == SubTimeFrame::TDC64H || fem_type == SubTimeFrame::TDC64H_V3);
                
                if (is_hr) max_ch = 64;
                else max_ch = 128;

                CheckAndCreateHistograms(fem_id, is_hr);

                for (uint64_t word : tdc_data) {
                    int type = -1;
                    int ch = -1;
                    int tot = -1;
                    int tdc_val = -1;

                    if (fem_type == SubTimeFrame::TDC64H || fem_type == SubTimeFrame::TDC64H_V3) {
                        struct TDC64H_V3::tdc64 tdc_struct;
                        type = TDC64H_V3::Unpack(word, &tdc_struct);
                        if (type == TDC64H_V3::T_TDC) {
                            ch = tdc_struct.ch;
                            tot = tdc_struct.tot;
                            tdc_val = tdc_struct.tdc; 
                        }
                    } else if (fem_type == SubTimeFrame::TDC64L || fem_type == SubTimeFrame::TDC64L_V3) {
                         struct TDC64L_V3::tdc64 tdc_struct;
                         type = TDC64L_V3::Unpack(word, &tdc_struct);
                         if (type == TDC64L_V3::T_TDC || type == TDC64L_V3::T_TDC_L || type == TDC64L_V3::T_TDC_T) {
                            ch = tdc_struct.ch;
                            tot = tdc_struct.tot;
                            tdc_val = tdc_struct.tdc;
                         }
                    }

                    if (ch >= 0 && ch < max_ch) {
                        multiplicity++;
                        fMapHitPattern[fem_id]->Fill(ch);
                        
                        // 1. まず「ps」単位に一度統一してから差分を計算する
                        // （※前提として、HRTDCのtdc_valは1ps/ch、LRTDCは(1ns=1024ps)として1024倍しています）
                        long tdc_ps = is_hr ? static_cast<long>(tdc_val) : static_cast<long>(tdc_val) * 1024;
                        
                        // 修正：LogicFilterで生成される trg_time は「tdc4n（約4.096ns = 4096ps単位）」のインデックスです。
                        // ps単位のtdc_psと差分をとるためには、4096を掛けてスケールを合わせる必要があります！
                        long trg_ps = static_cast<long>(trg_time & 0x1FFFFFFF) * 4096L;
                        
                        long diff_ps = tdc_ps - trg_ps; 
                        
                        // ロールオーバーの補正（統一したps基準での補正）
                        // ※trg_time や tdc_val の桁数（ビット幅）によっては0x10000000の補正値も合わせる必要があります
                        if (diff_ps < -0x10000000) diff_ps += 0x20000000;
                        if (diff_ps >  0x10000000) diff_ps -= 0x20000000;
                        
                        // 2. ヒストグラムに詰める際に、LRはns、HRはpsに直す
                        long final_diff = is_hr ? diff_ps : (diff_ps / 1024);
                        
                        fMapTDC[fem_id][ch]->Fill(final_diff);
                        fMapTOT[fem_id][ch]->Fill(tot);
                    }
                } 
                fMapMultiplicity[fem_id]->Fill(multiplicity);
            } 
        }
    }
    
    std::string ToIPAddress(uint32_t fem_id) {
        uint8_t* ip = reinterpret_cast<uint8_t*>(&fem_id);
        return std::to_string(ip[3]) + "." + std::to_string(ip[2]) + "." + std::to_string(ip[1]) + "." + std::to_string(ip[0]);
    }

    void CheckAndCreateHistograms(uint32_t fem_id, bool is_hr) {
        if (fMapHitPattern.count(fem_id) > 0) return;

        std::string ip_str = ToIPAddress(fem_id);
        int max_ch = is_hr ? 64 : 128;
        int max_pages = is_hr ? 4 : 8;

        LOG(info) << "Registering FEM ID: " << fem_id << " (" << ip_str << ") - " << (is_hr ? "HR" : "LR");

        // Hit Pattern
        fMapHitPattern[fem_id] = new TH1F(Form("h1_hitpat_%s", ip_str.c_str()), 
                                          Form("FEM %s Hit Pattern;Channel;Counts", ip_str.c_str()), 
                                          max_ch, 0, max_ch);
        
        // Multiplicity
        fMapMultiplicity[fem_id] = new TH1F(Form("h1_multi_%s", ip_str.c_str()), 
                                            Form("FEM %s Multiplicity;Hits/Event;Counts", ip_str.c_str()), 
                                            max_ch + 1, 0, max_ch + 1);

        // TDC & TOT Vectors
        fMapTDC[fem_id].resize(max_ch, nullptr);
        fMapTOT[fem_id].resize(max_ch, nullptr);

        // Summary Canvases 
        for (int page = 0; page < max_pages; ++page) {
            auto* c = new TCanvas(Form("c_tdc_%s_p%d", ip_str.c_str(), page), 
                                  Form("FEM %s TDC Ch %d-%d", ip_str.c_str(), page*16, (page+1)*16-1), 
                                  800, 800);
            c->Divide(4, 4);
            fCanvasesTDC[fem_id].push_back(c);
            fServer->Register(Form("/FEM_%s/Canvas_TDC", ip_str.c_str()), c);
        }

        for (int page = 0; page < max_pages; ++page) {
            auto* c = new TCanvas(Form("c_tot_%s_p%d", ip_str.c_str(), page), 
                                  Form("FEM %s TOT Ch %d-%d", ip_str.c_str(), page*16, (page+1)*16-1), 
                                  800, 800);
            c->Divide(4, 4);
            for(int p=1; p<=16; ++p) c->cd(p)->SetLogy(); // Set Log Y for TOT Summary
            fCanvasesTOT[fem_id].push_back(c);
            fServer->Register(Form("/FEM_%s/Canvas_TOT", ip_str.c_str()), c);
        }

        for (int i = 0; i < max_ch; ++i) {
            if (is_hr) {
                // HRTDC: TDC Range -100000000 to +100000000 (ps) -> +/- 100us
                fMapTDC[fem_id][i] = new TH1F(Form("h1_tdc_%s_ch%d", ip_str.c_str(), i), 
                                              Form("FEM %s TDC Ch%d;Rel Time [ps];Counts", ip_str.c_str(), i), 
                                              2000, -100000000, 100000000); 
                // HRTDC: TOT Range 0 to 200000
                fMapTOT[fem_id][i] = new TH1F(Form("h1_tot_%s_ch%d", ip_str.c_str(), i), 
                                              Form("FEM %s TOT Ch%d;TOT;Counts", ip_str.c_str(), i), 
                                              1000, 0, 200000); 
            } else {
                // LRTDC: TDC Range -1000 to +1000 (ns) -> +/- 1us
                fMapTDC[fem_id][i] = new TH1F(Form("h1_tdc_%s_ch%d", ip_str.c_str(), i), 
                                              Form("FEM %s TDC Ch%d;Rel Time [ns];Counts", ip_str.c_str(), i), 
                                              2000, -1000, 1000); 
                // LRTDC: TOT Range 0 to 200 (仮)
                fMapTOT[fem_id][i] = new TH1F(Form("h1_tot_%s_ch%d", ip_str.c_str(), i), 
                                              Form("FEM %s TOT Ch%d;TOT;Counts", ip_str.c_str(), i), 
                                              1000, 0, 200); 
            } 

            if (fServer) {
                fServer->Register(Form("/FEM_%s/TDC", ip_str.c_str()), fMapTDC[fem_id][i]);
                fServer->Register(Form("/FEM_%s/TOT", ip_str.c_str()), fMapTOT[fem_id][i]);
                fServer->SetItemField(Form("/FEM_%s/TOT/h1_tot_%s_ch%d", ip_str.c_str(), ip_str.c_str(), i), "_drawopt", "logy");
            }
            
            // Register to Canvas
            int page = i / 16;
            int pad = (i % 16) + 1;
            
            fCanvasesTDC[fem_id][page]->cd(pad);
            fMapTDC[fem_id][i]->Draw();
            
            fCanvasesTOT[fem_id][page]->cd(pad);
            fMapTOT[fem_id][i]->Draw();
        }

        if (fServer) {
            fServer->Register(Form("/FEM_%s", ip_str.c_str()), fMapHitPattern[fem_id]);
            fServer->Register(Form("/FEM_%s", ip_str.c_str()), fMapMultiplicity[fem_id]);
            fServer->SetItemField(Form("/FEM_%s", ip_str.c_str()), "_monitoring", "1000");
        }
    }
    
    void UpdateDisplay() {
        if (gSystem) gSystem->ProcessEvents(); 
    }

private:
    std::string fInputChannelName;
    TFSlicerUnpacker fUnpacker;
    uint64_t fMsgCount = 0;
    
    THttpServer* fServer = nullptr;
    KTimer fDrawTimer;

    // Histograms
    TH1F* fH1TrigInterval = nullptr;
    TH1F* fH1TrigTime     = nullptr;

    // Map: FEM ID -> Histograms
    std::map<uint32_t, TH1F*> fMapHitPattern;
    std::map<uint32_t, TH1F*> fMapMultiplicity;
    std::map<uint32_t, std::vector<TH1F*>> fMapTDC;
    std::map<uint32_t, std::vector<TH1F*>> fMapTOT;
    
    // Summary Canvases
    TCanvas* fCanvasSummary = nullptr;
    std::map<uint32_t, std::vector<TCanvas*>> fCanvasesTDC;
    std::map<uint32_t, std::vector<TCanvas*>> fCanvasesTOT;

    // Trigger Timing State
    uint64_t fLastTrigTime = 0;
    uint64_t fFirstTrigTime = 0;
};

void addCustomOptions(bpo::options_description& options)
{
    using opt = OnlineAnalysisTrigger::OptionKey;
    options.add_options()
        (opt::InputChannelName.data(), bpo::value<std::string>()->default_value("in"), "Name of the input channel");
}

std::unique_ptr<fair::mq::Device> getDevice(fair::mq::ProgOptions& /*config*/)
{
    return std::make_unique<OnlineAnalysisTrigger>();
}

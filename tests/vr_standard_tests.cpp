// Units that implement the cross-port Steam Frame VR standard on top of the
// vendored sfvr library: the [vr-perf] line, env overrides and refresh rate.
#include "starfox/vr/perf_log.hpp"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace starfox::vr;
namespace {
void require(bool value,const std::source_location where=std::source_location::current()) {
    if(!value) throw std::runtime_error("VR standard assertion failed at line "+std::to_string(where.line()));
}
void perf_line() {
    PerfLog log;
    PerfLog::Frame frame;
    frame.logic_ms=1;frame.model_ms=2;frame.upload_ms=.5;frame.layer_ms=.25;
    frame.eye_ms={2.,4.};
    // 10 s window: no line until the window has elapsed.
    for(int i=0;i<10;++i) {log.add_frame(i*.5,frame);require(!log.poll(i*.5));}
    // The 11th frame lands after the window and is the one that closes it.
    auto line=log.poll(10.0);
    require(line.has_value());
    require(line->starts_with("[vr-perf] fps=1.0 missed=0 cpu=3.75ms logic=1.00 model=2.00 upload=0.50 layer=0.25 eye=2.00/4.00ms gpu=n/a"));
    require(!log.poll(10.1)); // Next window has started.
    // GPU is the left+right sum when both eyes have one; missed frames count.
    frame.gpu_ms={3.,4.5};frame.missed=true;
    log.add_frame(10.5,frame);
    line=log.poll(20.5);
    require(line && line->find("missed=1")!=std::string::npos && line->find("gpu=7.50ms")!=std::string::npos);
    // Unmeasured stages count as zero rather than poisoning the means.
    PerfLog partial;PerfLog::Frame sparse;partial.add_frame(0,sparse);
    line=partial.poll(10.0);
    require(line && line->find("cpu=0.00ms logic=0.00")!=std::string::npos);
    // A stalled window still reports: a stall is data.
    require(partial.poll(20.0)->starts_with("[vr-perf] fps=0.0"));
}
}
int main() try {
    perf_line();
    std::cout<<"VR standard units passed (perf line)\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}

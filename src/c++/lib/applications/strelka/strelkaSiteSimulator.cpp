// -*- mode: c++; indent-tabs-mode: nil; -*-
//
// Starka
// Copyright (c) 2009-2014 Illumina, Inc.
//
// This software is provided under the terms and conditions of the
// Illumina Open Source Software License 1.
//
// You should have received a copy of the Illumina Open Source
// Software License 1 along with this program. If not, see
// <https://github.com/sequencing/licenses/>
//

///
/// \author Chris Saunders
///
/// note coding convention for all ranges '_pos fields' is:
/// XXX_begin_pos is zero-indexed position at the beginning of the range
/// XXX_end_pos is zero-index position 1 step after the end of the range
///


#include "strelkaSiteSimulator.hh"
#include "strelka_pile_test_run.hh"
#include "strelka_sim_test.hh"

#include "blt_util/blt_exception.hh"
#include "blt_util/log.hh"
#include "common/Exceptions.hh"

#include "boost/program_options.hpp"



void
strelkaSiteSimulator::
runInternal(int argc,char* argv[]) const
{
    strelka_options opt;
    strelka_site_sim_options sim_opt;

    for (int i(0); i<argc; ++i) {
        if (i) opt.cmdline += ' ';
        opt.cmdline += argv[i];
    }

    // mandatory setting:
    opt.is_user_genome_size=true;
    opt.user_genome_size=1;


    namespace po = boost::program_options;
    po::options_description req("configuration");
    req.add_options()
    ("total-sites", po::value(&sim_opt.total_sites)->default_value(sim_opt.total_sites),"number of sites to simulate")
    ("ncov", po::value(&sim_opt.ncov)->default_value(sim_opt.ncov),"normal depth")
    ("tcov", po::value(&sim_opt.tcov)->default_value(sim_opt.tcov),"tumor depth")
    ("tumor-purity", po::value(&sim_opt.tumor_purity)->default_value(sim_opt.tumor_purity),"tumor purity")
    ("somatic-only","only simulate somatic sites")
    ("seed",po::value<uint32_t>(&sim_opt.seed),"seed")
    ("qscores",po::value(&sim_opt.qval_file),"tab-delimited file specifying basecall qscore distribution (default: all basecalls are Q30)")
    ("gvcf",po::value(&sim_opt.is_somatic_gvcf)->zero_tokens(),"use somatic gvcf mode to compute scores for non-somatic sites")
    ;

    po::options_description visible("options");
    visible.add(req);

    bool po_parse_fail(false);
    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, visible), vm);
        po::notify(vm);
    } catch (const boost::program_options::error& e) {
        log_os << "\nERROR: Exception thrown by option parser: " << e.what() << "\n";
        po_parse_fail=true;
    }

    if ((argc<=1) || (vm.count("help")) || po_parse_fail) {
        log_os << "\n strelka site simulator...\n\n";
        log_os << "usage: program [options] > called\n\n";
        log_os << visible << "\n";
        exit(EXIT_FAILURE);
    }

    if (vm.count("somatic-only")) {
        sim_opt.mode=SIM_SOMATIC;
    }

    strelka_site_sim(opt,sim_opt);
    //    strelka_pile_test_run(opt);
}

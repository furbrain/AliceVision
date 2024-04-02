// This file is part of the AliceVision project.
// Copyright (c) 2024 AliceVision contributors.
// This Source Code Form is subject to the terms of the Mozilla Public License,
// v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include <aliceVision/cmdline/cmdline.hpp>
#include <aliceVision/system/Logger.hpp>
#include <aliceVision/system/main.hpp>
#include <aliceVision/fuseCut/InputSet.hpp>

#include <OpenMesh/Core/IO/reader/OBJReader.hh>
#include <OpenMesh/Core/IO/writer/OBJWriter.hh>
#include <OpenMesh/Core/IO/MeshIO.hh>
#include <OpenMesh/Core/Mesh/TriMesh_ArrayKernelT.hh>
#include <OpenMesh/Core/Geometry/VectorT.hh>
#include <OpenMesh/Tools/Decimater/DecimaterT.hh>
#include <OpenMesh/Tools/Decimater/ModQuadricT.hh>

#include <boost/program_options.hpp>
#include <fstream>


// These constants define the current software version.
// They must be updated when the command line is changed.
#define ALICEVISION_SOFTWARE_VERSION_MAJOR 1
#define ALICEVISION_SOFTWARE_VERSION_MINOR 0

using namespace aliceVision;

namespace po = boost::program_options;

bool computeSubMesh(const std::string & outputMeshPath, const std::string & inputMeshPath, const Eigen::Vector3d & bbMin, const Eigen::Vector3d & bbMax)
{
    typedef OpenMesh::TriMesh_ArrayKernelT<> Mesh;
    typedef OpenMesh::Decimater::DecimaterT<Mesh> Decimater;
    typedef OpenMesh::Decimater::ModQuadricT<Mesh>::Handle HModQuadric;

    Mesh mesh;
    if (!OpenMesh::IO::read_mesh(mesh, inputMeshPath))
    {
        ALICEVISION_LOG_ERROR("Unable to read input mesh from the file: " << inputMeshPath);
        return false;
    }

    ALICEVISION_LOG_INFO("Mesh file: \"" << inputMeshPath << "\" loaded.");

    const double tolerance = 0.001;

    while (1)
    {
        int nbInputPoints = mesh.n_vertices();
        int minNbInputPoints = nbInputPoints / 2;

        Decimater decimater(mesh);
        HModQuadric hModQuadric;
        decimater.add(hModQuadric);
        decimater.module(hModQuadric).set_max_err(tolerance * tolerance);
        decimater.initialize();        
        size_t removedVertices = decimater.decimate_to(minNbInputPoints);
        decimater.mesh().garbage_collection();

        if (mesh.n_faces() == 0)
        {
            ALICEVISION_LOG_ERROR("Failed: the output mesh is empty.");
            return false;
        }

        if (nbInputPoints == mesh.n_vertices())
        {
            break;
        }

        ALICEVISION_LOG_INFO("Before : " << nbInputPoints);
        ALICEVISION_LOG_INFO("After : " << mesh.n_vertices());
    }

    ALICEVISION_LOG_INFO("Save mesh.");
    if (!OpenMesh::IO::write_mesh(mesh, outputMeshPath))
    {
        ALICEVISION_LOG_ERROR("Failed to save mesh \"" << outputMeshPath << "\".");
        return false;
    }

    

    return true;
}

int aliceVision_main(int argc, char* argv[])
{
    system::Timer timer;
    int rangeStart = -1;
    int rangeSize = 1;

    std::string jsonFilename = "";
    std::string outputDirectory = "";
    std::string outputJsonFilename = "";

    // clang-format off
    po::options_description requiredParams("Required parameters");
    requiredParams.add_options()
        ("input,i", po::value<std::string>(&jsonFilename)->required(),
         "json file.")
        ("output,o", po::value<std::string>(&outputDirectory)->required(),
         "Output directory.")
        ("outputJson", po::value<std::string>(&outputJsonFilename)->required(),
         "Output scene description.");

    po::options_description optionalParams("Optional parameters");
    optionalParams.add_options()
        ("rangeStart", po::value<int>(&rangeStart)->default_value(rangeStart),
         "Range image index start.")
        ("rangeSize", po::value<int>(&rangeSize)->default_value(rangeSize),
         "Range size.");
    // clang-format on

    CmdLine cmdline("AliceVision lidarMeshing");
    cmdline.add(requiredParams);
    cmdline.add(optionalParams);
    if (!cmdline.execute(argc, argv))
    {
        return EXIT_FAILURE;
    }

    std::ifstream inputfile(jsonFilename);
    if (!inputfile.is_open())
    {
        ALICEVISION_LOG_ERROR("Cannot open json input file");
        return EXIT_FAILURE;
    }
    
    
    std::stringstream buffer;
    buffer << inputfile.rdbuf();
    boost::json::value jv = boost::json::parse(buffer.str());
    fuseCut::InputSet inputsets(boost::json::value_to<fuseCut::InputSet>(jv));

    size_t setSize = inputsets.size();
    if (rangeStart != -1)
    {
        if (rangeStart < 0 || rangeSize < 0)
        {
            ALICEVISION_LOG_ERROR("Range is incorrect");
            return EXIT_FAILURE;
        }

        if (rangeStart + rangeSize > setSize)
            rangeSize = setSize - rangeStart;

        if (rangeSize <= 0)
        {
            ALICEVISION_LOG_WARNING("Nothing to compute.");
            return EXIT_SUCCESS;
        }
    }
    else
    {
        rangeStart = 0;
        rangeSize = setSize;
    }

    for (int idSub = rangeStart; idSub < rangeStart + rangeSize; idSub++)
    {
        const fuseCut::Input & input = inputsets[idSub];
        std::string ss = outputDirectory + "/subobj_" + std::to_string(idSub) + ".obj";

        ALICEVISION_LOG_INFO("Computing sub mesh " << idSub + 1 << " / " << setSize);
        if (!computeSubMesh(ss, input.subMeshPath, input.bbMin, input.bbMax))
        {
            ALICEVISION_LOG_ERROR("Error computing sub mesh");
            return EXIT_FAILURE;
        }
    }

    std::ofstream of(outputJsonFilename);
    jv = boost::json::value_from(inputsets);
    of << boost::json::serialize(jv);
    of.close();

    
    return EXIT_SUCCESS;
}
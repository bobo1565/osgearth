/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2008-2013 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/

#include <osgDB/FileNameUtils>
#include <osgDB/FileUtils>

#include <osg/io_utils>

#include <osgEarth/Common>
#include <osgEarth/Cache>
#include <osgEarth/CacheEstimator>
#include <osgEarth/CacheSeed>
#include <osgEarth/MapNode>
#include <osgEarth/Registry>
#include <osgEarthDrivers/feature_ogr/OGRFeatureOptions>
#include <osgEarth/FileUtils>

#include <osgEarth/TileVisitor>

#include <iostream>
#include <sstream>
#include <iterator>

using namespace osgEarth;
using namespace osgEarth::Drivers;

#define LC "[osgearth_cache] "

int list( osg::ArgumentParser& args );
int seed( osg::ArgumentParser& args );
int purge( osg::ArgumentParser& args );
int usage( const std::string& msg );
int message( const std::string& msg );


int
    main(int argc, char** argv)
{
    osg::ArgumentParser args(&argc,argv);

    if ( args.read( "--seed") )
        return seed( args );
    else if ( args.read( "--list" ) )
        return list( args );
    else if ( args.read( "--purge" ) )
        return purge( args );        
    else
    return usage("");
}

int
    usage( const std::string& msg )
{
    if ( !msg.empty() )
    {
        std::cout << msg << std::endl;
    }

    std::cout
        << std::endl
        << "USAGE: osgearth_cache" << std::endl
        << std::endl
        << "    --list file.earth                   ; Lists info about the cache in a .earth file" << std::endl       
        << std::endl
        << "    --seed file.earth                   ; Seeds the cache in a .earth file"  << std::endl
        << "        [--estimate]                    ; Print out an estimation of the number of tiles, disk space and time it will take to perform this seed operation" << std::endl
        << "        [--min-level level]             ; Lowest LOD level to seed (default=0)" << std::endl
        << "        [--max-level level]             ; Highest LOD level to seed (defaut=highest available)" << std::endl
        << "        [--bounds xmin ymin xmax ymax]* ; Geospatial bounding box to seed (in map coordinates; default=entire map)" << std::endl
        << "        [--index shapefile]             ; Use the feature extents in a shapefile to set the bounding boxes for seeding" << std::endl
        << "        [--cache-path path]             ; Overrides the cache path in the .earth file" << std::endl
        << "        [--cache-type type]             ; Overrides the cache type in the .earth file" << std::endl
        << "        [--threads]                     ; The number of threads to use for the seed operation (default=1)" << std::endl
        << "        [--verbose]                     ; Displays progress of the seed operation" << std::endl
        << std::endl
        << "    --purge file.earth                  ; Purges a layer cache in a .earth file (interactive)" << std::endl
        << std::endl;

    return -1;
}

int message( const std::string& msg )
{
    if ( !msg.empty() )
    {
        std::cout << msg << std::endl << std::endl;
    }
    return 0;
}

int
    seed( osg::ArgumentParser& args )
{    
    osgDB::Registry::instance()->getReaderWriterForExtension("png");
    osgDB::Registry::instance()->getReaderWriterForExtension("jpg");
    osgDB::Registry::instance()->getReaderWriterForExtension("tiff");

    //Read the min level
    unsigned int minLevel = 0;
    while (args.read("--min-level", minLevel));

    //Read the max level
    unsigned int maxLevel = 5;
    while (args.read("--max-level", maxLevel));

    bool estimate = args.read("--estimate");        
    

    std::vector< Bounds > bounds;
    // restrict packaging to user-specified bounds.    
    double xmin=DBL_MAX, ymin=DBL_MAX, xmax=DBL_MIN, ymax=DBL_MIN;
    while (args.read("--bounds", xmin, ymin, xmax, ymax ))
    {        
        Bounds b;
        b.xMin() = xmin, b.yMin() = ymin, b.xMax() = xmax, b.yMax() = ymax;
        bounds.push_back( b );
    }    

    std::string tileList;
    while (args.read( "--tiles", tileList ) );

    //Read the cache override directory
    std::string cachePath;
    while (args.read("--cache-path", cachePath));

    //Read the cache type
    std::string cacheType;
    while (args.read("--cache-type", cacheType));

    bool verbose = args.read("--verbose");

    unsigned int batchSize = 0;
    args.read("--batchsize", batchSize);

    // Read the concurrency level
    unsigned int concurrency = 0;
    args.read("-c", concurrency);
    args.read("--concurrency", concurrency);

    int imageLayerIndex = -1;
    args.read("--image", imageLayerIndex);

    int elevationLayerIndex = -1;
    args.read("--elevation", elevationLayerIndex);


    //Read in the earth file.
    osg::ref_ptr<osg::Node> node = osgDB::readNodeFiles( args );
    if ( !node.valid() )
        return usage( "Failed to read .earth file." );

    MapNode* mapNode = MapNode::findMapNode( node.get() );
    if ( !mapNode )
        return usage( "Input file was not a .earth file" );

    // Read in an index shapefile
    std::string index;
    while (args.read("--index", index))
    {        
        //Open the feature source
        OGRFeatureOptions featureOpt;
        featureOpt.url() = index;        

        osg::ref_ptr< FeatureSource > features = FeatureSourceFactory::create( featureOpt );
        features->initialize();
        features->getFeatureProfile();

        osg::ref_ptr< FeatureCursor > cursor = features->createFeatureCursor();
        while (cursor.valid() && cursor->hasMore())
        {
            osg::ref_ptr< Feature > feature = cursor->nextFeature();
            osgEarth::Bounds featureBounds = feature->getGeometry()->getBounds();
            GeoExtent ext( feature->getSRS(), featureBounds );
            ext = ext.transform( mapNode->getMapSRS() );
            bounds.push_back( ext.bounds() );            
        }
    }

    // If they requested to do an estimate then don't do the the seed, just print out the estimated values.
    if (estimate)
    {        
        CacheEstimator est;
        est.setMinLevel( minLevel );
        est.setMaxLevel( maxLevel );
        est.setProfile( mapNode->getMap()->getProfile() );

        for (unsigned int i = 0; i < bounds.size(); i++)
        {
            GeoExtent extent(mapNode->getMapSRS(), bounds[i]);
            OE_DEBUG << "Adding extent " << extent.toString() << std::endl;
            est.addExtent( extent );
        } 

        unsigned int numTiles = est.getNumTiles();
        double size = est.getSizeInMB();
        double time = est.getTotalTimeInSeconds();
        std::cout << "Cache Estimation " << std::endl
            << "---------------- " << std::endl
            << "Total number of tiles: " << numTiles << std::endl
            << "Size on disk:          " << osgEarth::prettyPrintSize( size ) << std::endl
            << "Total time:            " << osgEarth::prettyPrintTime( time ) << std::endl;

        return 0;
    }
    
    osg::ref_ptr< TileVisitor > visitor;


    // If we are given a task file, load it up and create a new TileKeyListVisitor
    if (!tileList.empty())
    {        
        TaskList tasks( mapNode->getMap()->getProfile() );
        tasks.load( tileList );

        TileKeyListVisitor* v = new TileKeyListVisitor();
        v->setKeys( tasks.getKeys() );
        visitor = v;        
        OE_DEBUG << "Read task list with " << tasks.getKeys().size() << " tasks" << std::endl;
    }
  

    // If we dont' have a visitor create one.
    if (!visitor.valid())
    {
        if (args.read("--mt"))
        {
            // Create a multithreaded visitor
            MultithreadedTileVisitor* v = new MultithreadedTileVisitor();
            if (concurrency > 0)
            {
                v->setNumThreads(concurrency);
            }
            visitor = v;            
        }
        else if (args.read("--mp"))
        {
            // Create a multiprocess visitor
            MultiprocessTileVisitor* v = new MultiprocessTileVisitor();
            if (concurrency > 0)
            {
                v->setNumProcesses(concurrency);
            }

            if (batchSize > 0)
            {                
                v->setBatchSize(batchSize);
            }

            
            // Try to find the earth file
            std::string earthFile;
            for(int pos=1;pos<args.argc();++pos)
            {
                if (!args.isOption(pos))
                {
                    earthFile  = args[ pos ];
                }
            }
            std::stringstream baseCommand;
            baseCommand << "osgearth_cache2 --seed ";     
            if (imageLayerIndex >= 0)
            {
                baseCommand << " --image " << imageLayerIndex;
            }
            else if (elevationLayerIndex >= 0)
            {
                baseCommand << " --elevation " << elevationLayerIndex;
            }
            baseCommand << " " << earthFile;                     
            v->setBaseCommand(baseCommand.str());
            visitor = v;            
        }
        else
        {
            // Create a single thread visitor
            visitor = new TileVisitor();            
        }        
    }

    osg::ref_ptr< ProgressCallback > progress = new ConsoleProgressCallback();
    
    if (verbose)
    {
        visitor->setProgressCallback( progress );
    }

    visitor->setMinLevel( minLevel );
    visitor->setMaxLevel( maxLevel );        


    for (unsigned int i = 0; i < bounds.size(); i++)
    {
        GeoExtent extent(mapNode->getMapSRS(), bounds[i]);
        OE_DEBUG << "Adding extent " << extent.toString() << std::endl;                
        visitor->addExtent( extent );
    }    
    

    // Initialize the seeder
    CacheSeed seeder;
    seeder.setVisitor(visitor.get());

    osgEarth::Map* map = mapNode->getMap();

    // They want to seed an image layer
    if (imageLayerIndex >= 0)
    {
        osg::ref_ptr< ImageLayer > layer = map->getImageLayerAt( imageLayerIndex );
        if (layer)
        {
            OE_NOTICE << "Seeding single layer " << layer->getName() << std::endl;
            osg::Timer_t start = osg::Timer::instance()->tick();        
            seeder.run(layer, map->getProfile());
            osg::Timer_t end = osg::Timer::instance()->tick();
            if (verbose)
            {
                OE_NOTICE << "Completed seeding layer " << layer->getName() << " in " << prettyPrintTime( osg::Timer::instance()->delta_s( start, end ) ) << std::endl;
            }    
        }
        else
        {
            std::cout << "Failed to find an image layer at index " << imageLayerIndex << std::endl;
            return 1;
        }

    }
    // They want to seed an elevation layer
    else if (elevationLayerIndex >= 0)
    {
        osg::ref_ptr< ElevationLayer > layer = map->getElevationLayerAt( elevationLayerIndex );
        if (layer)
        {
            OE_NOTICE << "Seeding single layer " << layer->getName() << std::endl;
            osg::Timer_t start = osg::Timer::instance()->tick();        
            seeder.run(layer, map->getProfile());
            osg::Timer_t end = osg::Timer::instance()->tick();
            if (verbose)
            {
                OE_NOTICE << "Completed seeding layer " << layer->getName() << " in " << prettyPrintTime( osg::Timer::instance()->delta_s( start, end ) ) << std::endl;
            }    
        }
        else
        {
            std::cout << "Failed to find an elevation layer at index " << elevationLayerIndex << std::endl;
            return 1;
        }
    }
    // They want to seed the entire map
    else
    {        
        MultiprocessTileVisitor* mp = dynamic_cast<MultiprocessTileVisitor*>(seeder.getVisitor());
        std::string baseCommand;
        if (mp)
        {
            baseCommand = mp->getBaseCommand();
        }
        // Seed all the map layers
        for (unsigned int i = 0; i < map->getNumImageLayers(); ++i)
        {            
            osg::ref_ptr< ImageLayer > layer = map->getImageLayerAt(i);
            OE_NOTICE << "Seeding layer" << layer->getName() << std::endl;
            if (mp)
            {                
                std::stringstream buf;
                buf << baseCommand << " --image " << i;                
                mp->setBaseCommand(buf.str());
            }

            osg::Timer_t start = osg::Timer::instance()->tick();
            seeder.run(layer.get(), map->getProfile());            
            osg::Timer_t end = osg::Timer::instance()->tick();
            if (verbose)
            {
                OE_NOTICE << "Completed seeding layer " << layer->getName() << " in " << prettyPrintTime( osg::Timer::instance()->delta_s( start, end ) ) << std::endl;
            }                
        }

        for (unsigned int i = 0; i < map->getNumElevationLayers(); ++i)
        {
            osg::ref_ptr< ElevationLayer > layer = map->getElevationLayerAt(i);
            OE_NOTICE << "Seeding layer" << layer->getName() << std::endl;
            if (mp)
            {                
                std::stringstream buf;
                buf << baseCommand << " --elevation " << i;                
                mp->setBaseCommand(buf.str());
            }
            osg::Timer_t start = osg::Timer::instance()->tick();
            seeder.run(layer.get(), map->getProfile());            
            osg::Timer_t end = osg::Timer::instance()->tick();
            if (verbose)
            {
                OE_NOTICE << "Completed seeding layer " << layer->getName() << " in " << prettyPrintTime( osg::Timer::instance()->delta_s( start, end ) ) << std::endl;
            }                
        }        
    }    

    return 0;
}

int
    list( osg::ArgumentParser& args )
{
    osg::ref_ptr<osg::Node> node = osgDB::readNodeFiles( args );
    if ( !node.valid() )
        return usage( "Failed to read .earth file." );

    MapNode* mapNode = MapNode::findMapNode( node.get() );
    if ( !mapNode )
        return usage( "Input file was not a .earth file" );

    Map* map = mapNode->getMap();
    const Cache* cache = map->getCache();

    if ( !cache )
        return message( "Earth file does not contain a cache." );

    std::cout 
        << "Cache config: " << std::endl
        << cache->getCacheOptions().getConfig().toJSON(true) << std::endl;

    MapFrame mapf( mapNode->getMap() );

    TerrainLayerVector layers;
    std::copy( mapf.imageLayers().begin(), mapf.imageLayers().end(), std::back_inserter(layers) );
    std::copy( mapf.elevationLayers().begin(), mapf.elevationLayers().end(), std::back_inserter(layers) );

    for( TerrainLayerVector::iterator i =layers.begin(); i != layers.end(); ++i )
    {
        TerrainLayer* layer = i->get();
        TerrainLayer::CacheBinMetadata meta;

        bool useMFP =
            layer->getProfile() &&
            layer->getProfile()->getSRS()->isSphericalMercator() &&
            mapNode->getMapNodeOptions().getTerrainOptions().enableMercatorFastPath() == true;

        const Profile* cacheProfile = useMFP ? layer->getProfile() : map->getProfile();

        if ( layer->getCacheBinMetadata( cacheProfile, meta ) )
        {
            Config conf = meta.getConfig();
            std::cout << "Layer \"" << layer->getName() << "\", cache metadata =" << std::endl
                << conf.toJSON(true) << std::endl;
        }
        else
        {
            std::cout << "Layer \"" << layer->getName() << "\": no cache information" 
                << std::endl;
        }
    }

    return 0;
}

struct Entry
{
    bool                   _isImage;
    std::string            _name;
    osg::ref_ptr<CacheBin> _bin;
};


int
    purge( osg::ArgumentParser& args )
{
    //return usage( "Sorry, but purge is not yet implemented." );

    osg::ref_ptr<osg::Node> node = osgDB::readNodeFiles( args );
    if ( !node.valid() )
        return usage( "Failed to read .earth file." );

    MapNode* mapNode = MapNode::findMapNode( node.get() );
    if ( !mapNode )
        return usage( "Input file was not a .earth file" );

    Map* map = mapNode->getMap();

    if ( !map->getCache() )
        return message( "Earth file does not contain a cache." );

    std::vector<Entry> entries;


    ImageLayerVector imageLayers;
    map->getImageLayers( imageLayers );
    for( ImageLayerVector::const_iterator i = imageLayers.begin(); i != imageLayers.end(); ++i )
    {
        ImageLayer* layer = i->get();

        bool useMFP =
            layer->getProfile() &&
            layer->getProfile()->getSRS()->isSphericalMercator() &&
            mapNode->getMapNodeOptions().getTerrainOptions().enableMercatorFastPath() == true;

        const Profile* cacheProfile = useMFP ? layer->getProfile() : map->getProfile();

        CacheBin* bin = layer->getCacheBin( cacheProfile );
        if ( bin )
        {
            entries.push_back(Entry());
            entries.back()._isImage = true;
            entries.back()._name = i->get()->getName();
            entries.back()._bin = bin;
        }
    }

    ElevationLayerVector elevationLayers;
    map->getElevationLayers( elevationLayers );
    for( ElevationLayerVector::const_iterator i = elevationLayers.begin(); i != elevationLayers.end(); ++i )
    {
        ElevationLayer* layer = i->get();

        bool useMFP =
            layer->getProfile() &&
            layer->getProfile()->getSRS()->isSphericalMercator() &&
            mapNode->getMapNodeOptions().getTerrainOptions().enableMercatorFastPath() == true;

        const Profile* cacheProfile = useMFP ? layer->getProfile() : map->getProfile();

        CacheBin* bin = i->get()->getCacheBin( cacheProfile );
        if ( bin )
        {
            entries.push_back(Entry());
            entries.back()._isImage = false;
            entries.back()._name = i->get()->getName();
            entries.back()._bin = bin;
        }
    }

    if ( entries.size() > 0 )
    {
        std::cout << std::endl;

        for( unsigned i=0; i<entries.size(); ++i )
        {
            std::cout << (i+1) << ") " << entries[i]._name << " (" << (entries[i]._isImage? "image" : "elevation" ) << ")" << std::endl;
        }

        std::cout 
            << std::endl
            << "Enter number of cache to purge, or <enter> to quit: "
            << std::flush;

        std::string input;
        std::getline( std::cin, input );

        if ( !input.empty() )
        {
            unsigned k = as<unsigned>(input, 0L);
            if ( k > 0 && k <= entries.size() )
            {
                Config meta = entries[k-1]._bin->readMetadata();
                if ( !meta.empty() )
                {
                    std::cout
                        << std::endl
                        << "Cache METADATA:" << std::endl
                        << meta.toJSON() 
                        << std::endl << std::endl;
                }

                std::cout
                    << "Are you sure (y/N)? "
                    << std::flush;

                std::getline( std::cin, input );
                if ( input == "y" || input == "Y" )
                {
                    std::cout << "Purging.." << std::flush;
                    entries[k-1]._bin->purge();
                }
                else
                {
                    std::cout << "No action taken." << std::endl;
                }
            }
            else
            {
                std::cout << "Invalid choice." << std::endl;
            }
        }
        else
        {
            std::cout << "No action taken." << std::endl;
        }
    }

    return 0;
}
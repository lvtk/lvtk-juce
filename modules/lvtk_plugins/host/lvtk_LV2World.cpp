/*
    This file is part of the lvtk_plugins JUCE module
    Copyright (C) 2013  Michael Fisher <mfisher31@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

LV2World::LV2World()
{
    world = lilv_world_new();
    lilv_world_load_all (world);
    
    lv2_InputPort   = lilv_new_uri (world, LV2_CORE__InputPort);
    lv2_OutputPort  = lilv_new_uri (world, LV2_CORE__OutputPort);
    lv2_AudioPort   = lilv_new_uri (world, LV2_CORE__AudioPort);
    lv2_AtomPort    = lilv_new_uri (world, LV2_ATOM__AtomPort);
    lv2_ControlPort = lilv_new_uri (world, LV2_CORE__ControlPort);
    lv2_EventPort   = lilv_new_uri (world, LV2_EVENT__EventPort);
    lv2_CVPort      = lilv_new_uri (world, LV2_CORE__CVPort);
    midi_MidiEvent  = lilv_new_uri (world, LV2_MIDI__MidiEvent);
    work_schedule   = lilv_new_uri (world, LV2_WORKER__schedule);
    work_interface  = lilv_new_uri (world, LV2_WORKER__interface);
    
    currentThread = 0;
    numThreads    = 2;
}

LV2World::~LV2World()
{
#define _node_free(n) lilv_node_free (const_cast<LilvNode*> (n))
    _node_free (lv2_InputPort);
    _node_free (lv2_OutputPort);
    _node_free (lv2_AudioPort);
    _node_free (lv2_AtomPort);
    _node_free (lv2_ControlPort);
    _node_free (lv2_EventPort);
    _node_free (lv2_CVPort);
    _node_free (midi_MidiEvent);
    _node_free (work_schedule);
    _node_free (work_interface);
    
    lilv_world_free (world);
    world = nullptr;
}

LV2Module*
LV2World::createModule (const String& uri)
{
    if (const LilvPlugin* plugin = getPlugin (uri))
        return new LV2Module (*this, plugin);
    return nullptr;
}

LV2PluginModel*
LV2World::createPluginModel (const String& uri)
{
    if (const LilvPlugin* plugin = getPlugin (uri))
        return new LV2PluginModel (*this, plugin);
    return nullptr;
}

const LilvPlugin*
LV2World::getPlugin (const String& uri) const
{
    LilvNode* p (lilv_new_uri (world, uri.toUTF8()));
    const LilvPlugin* plugin = lilv_plugins_get_by_uri (getAllPlugins(), p);
    lilv_node_free (p);

    return plugin;
}

const LilvPlugins*
LV2World::getAllPlugins() const
{
    return lilv_world_get_all_plugins (world);
}

WorkThread&
LV2World::getWorkThread()
{
    if (threads.size() <= currentThread) {
        threads.add (new WorkThread ("LV2 Worker " + String(currentThread + 1), 2048));
        threads.getLast()->setPriority (5);
    }
    
    const int32 threadIndex = currentThread;
    if (++currentThread >= numThreads)
        currentThread = 0;
    
    return *threads.getUnchecked (threadIndex);
}

bool
LV2World::isFeatureSupported (const String& featureURI)
{
   if (features.contains (featureURI))
      return true;

   if (featureURI == LV2_WORKER__schedule)
      return true;

   return false;
}

bool
LV2World::isPluginAvailable (const String& uri)
{
    return (getPlugin (uri) != nullptr);
}

bool
LV2World::isPluginSupported (const String& uri)
{
    if (const LilvPlugin * plugin = getPlugin (uri))
        return isPluginSupported (plugin);
    return false;
}

bool
LV2World::isPluginSupported (const LilvPlugin* plugin)
{
    // Required features support
    LilvNodes* nodes = lilv_plugin_get_required_features (plugin);
    LILV_FOREACH (nodes, iter, nodes)
    {
        const LilvNode* node (lilv_nodes_get (nodes, iter));
        if (! isFeatureSupported (CharPointer_UTF8 (lilv_node_as_uri (node)))) {
            return false; // Feature not supported
        }
    }
    lilv_nodes_free (nodes); nodes = nullptr;
    
    
    // Check this plugin's port types are supported
    const uint32 numPorts = lilv_plugin_get_num_ports (plugin);
    for (uint32 i = 0; i < numPorts; ++i)
    {
        // const LilvPort* port (lilv_plugin_get_port_by_index (plugin, i));
        // nothing here yet (or ever)
    }
    
    return true;
}

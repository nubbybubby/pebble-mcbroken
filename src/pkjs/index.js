var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

/* This code is an NSFW warning (it sucks) */

var URL = 'https://data.mcbroken.com'
var MARKERS = '/markers.json'
var STATS = '/stats.json'

var xhr_markers;
var xhr_stats;

var id_gps;
var current_id;
var send_id;

let cache_max_age = 60 // seconds

let markers_cache = [];
let stats_cache = [];
let markers_then = [];
let stats_then = [];

const error = Object.freeze({
    connection_timed_out: "mcConnection timed out.",
    could_not_connect: "Could not connect to mcbroken.",
    could_not_parse: "Could not parse mcData.",
    no_gps: "Could not get location.",
    no_loc_saved: "No locations saved!",
    no_loc_found: "No locations found!"
});

const request = Object.freeze({
    type_markers: 0,
    type_stats: 1
});

const not_found_feature = {
    geometry: { coordinates: [0,0], type: 'Point' },
    properties: {
        is_broken: true,
        is_active: false,
        dot: '...',
        state: null,
        city: 'Check address',
        street: 'Location not found',
        country: null,
        last_checked: 'Checked 67 minutes ago' // laugh
    },
    type: 'Feature'
};

function mcSend(messages, id) {
    if (id !== undefined) {
        send_id = id;
    }

    setTimeout(() => {
        if (messages.length === 0 || send_id !== current_id) return;
        message = messages.shift();
        Pebble.sendAppMessage(message, function() {
            mcSend(messages);
        },
        function (e) {
            console.log("I've McFallen! I'm Sorry! I've McFallen!");
        });
    }, 50);
}

function sendmcError(error_message, id) {
    const message = { 
        'mc_message': "mc_error",
        'error': error_message,
        'id': id
    };
    if (id !== current_id) return;
    Pebble.sendAppMessage(message);
    current_id = 0;
}

function undefine_xhr(type) {
    if (!type) {
        xhr_markers = undefined;
    } else {
        xhr_stats = undefined;
    }
}

function mcRequest(type) {
    return new Promise((resolve) => { 
        const now = new Date().getTime();

        let cache = [];
        let then = [];

        if (!type) {
            cache = markers_cache;
            then = markers_then;
        } else {
            cache = stats_cache;
            then = stats_then;
        }

        if (Object.keys(cache).length > 0 && now - then < cache_max_age * 1000) {
            resolve(cache);
        } else if (Object.keys(cache).length > 0 && now - then >= cache_max_age * 1000) {
            undefine_xhr(type);
        }
        
        if (!type) {
            if (xhr_markers) return;
            xhr_markers = new XMLHttpRequest();
            var xhr = xhr_markers;
            xhr.open('GET', URL + MARKERS, true);
        } else {
            if (xhr_stats) return;
            xhr_stats = new XMLHttpRequest();
            var xhr = xhr_stats;
            xhr.open('GET', URL + STATS, true);
        }

        xhr.timeout = 10000;                
        xhr.setRequestHeader('Content-Type', 'application/json');
        xhr.send();

        xhr.onload = function() {
            if (xhr && xhr.status === 200 && xhr.readyState === 4) {
                try {
                    cache = JSON.parse(xhr.responseText);
                } catch (error) {
                    console.log(error);
                    sendmcError(error.could_not_parse, current_id);
                    undefine_xhr(type);
                    cache = [];
                    return;
                }

                then = new Date().getTime();

                if (!type) {
                    markers_cache = cache;
                    markers_then = then;
                } else {
                    stats_cache = cache;
                    stats_then = then;
                }

                resolve(cache);
            }
        };
        xhr.onloadend = function() {
            if (xhr && xhr.status == 404) {
                sendmcError(error.could_not_connect, current_id);
                undefine_xhr(type);
                return;
            }
        }
        xhr.onerror = function() {
            sendmcError(error.could_not_connect, current_id);
            undefine_xhr(type);
            return;
        }
        xhr.ontimeout = function() {
            sendmcError(error.connection_timed_out, current_id);
            undefine_xhr(type);
            return;
        }
    });
}

function mcCalculateDistance(mc_location, current_loc) {
    /* I totally wrote this out. I definitely did NOT copy and paste 
       from google AI search overview */
    
    const toRad = (value) => (value * Math.PI) / 180;
    const R = 6371;

    const dLat = toRad(mc_location[1] - current_loc[0]);
    const dLon = toRad(mc_location[0] - current_loc[1]);

    const a =
        Math.sin(dLat / 2) * Math.sin(dLat / 2) +
        Math.cos(toRad(current_loc[0])) * Math.cos(toRad(mc_location[1])) *
        Math.sin(dLon / 2) * Math.sin(dLon / 2);

    const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
    return R * c;
}

function format_and_send(type, result, id) {
    let index = 0;

    const message = [];
    
    const keys_markers = [ 'dot', 'city', 'street', 'last_checked' ];
    const keys_stats = [ 'city', 'broken', 'total_locations' ];

    var keys = [];
    var mc_message_string = [];

    switch (type) {
        case 0:
            keys = keys_markers;
            mc_message_string = "mc_marker_data";
            result.forEach(feature => {
                if (feature.properties) {
                    message.push(feature.properties);
                }
            });
            break;
        case 1:
            keys = keys_stats;
            mc_message_string = "mc_stat_data";
            
            result.forEach(city => {
                message.push(city);
            });
            break;
    }
    
    const filtered_message = message.map(obj => {
        const new_message = {};

        keys.forEach(key => {
            if (obj.hasOwnProperty(key)) {
                if (key === 'total_locations') {
                    var location_int = parseInt(obj[key]);
                    if (isNaN(location_int)) {
                        new_message[key] = 0;
                    } else {
                        new_message[key] = location_int;
                    }
                } else {
                    new_message[key] = obj[key].toString();
                }
            } else {
                if (key === 'total_locations') {
                    new_message[key] = 0;
                } else if (key === 'broken') {
                    new_message[key] = '0';
                } else {
                    new_message[key] = `no ${[key]}`;
                }
            }
        });

        new_message['mc_message'] = mc_message_string;
        new_message['index'] = index;
        new_message['count'] = message.length;
        new_message['id'] = id;
        
        index++;
        return new_message;
    });

    if (message.length > 0) {
        mcSend(filtered_message, id);
    } else {
        sendmcError(error.no_loc_found, id); 
    }
}

function fetch_mcdata_and_sort_by_saved(id) {
    let max_saved_mc_count = 5
    
    try {
        var settings = JSON.parse(localStorage.getItem("clay-settings"));
    } catch (error) {
        console.log(error);
    }
    
    if (!settings) {
        sendmcError(error.no_loc_saved, id);
        return;
    }

    const streets_input_arr = [ 
        settings.mc_save_slot_1,
        settings.mc_save_slot_2,
        settings.mc_save_slot_3,
        settings.mc_save_slot_4,
        settings.mc_save_slot_5
    ];

    if (streets_input_arr.every(element => element === "")) {
        sendmcError(error.no_loc_saved, id); 
        return;
    }

    const streets_input = streets_input_arr.map(element => element.toLowerCase().trim());
    
    const streets = streets_input.filter(Boolean);

    mcRequest(request.type_markers)
        .then(function(mcdata) {
            if (id !== current_id) return;

            if ('features' in mcdata === false) {
                format_and_send([], id);
                return;
            }

            const results = [];
       
            /* This is kinda jank and inefficient. Whatever, it's fast enough */
            let has_pushed;
            for (let i = 0; i < streets.filter(Boolean).length; i++) {
                has_pushed = false;
                mcdata.features.filter(feature => {
                    if (!feature.properties || !feature.properties.street) return;
                    if (feature.properties.street.toLowerCase().trim().includes(streets[i])) {
                        if (has_pushed || streets[i].length < 4) return;
                        results.push(feature);
                        has_pushed = true;
                    }
                });
                if (!has_pushed) {
                    /* I'm doing the parse stringify workaround 
                        because structuredClone doesn't work in the emulator */
                    results.push(JSON.parse(JSON.stringify(not_found_feature)));
                }
            }
        
            const results_sliced = new Set(results.slice(0, max_saved_mc_count));

            format_and_send(0, results_sliced, id);
        });
}

function fetch_mcdata_and_sort_by_location(coords, id) {
    let radius = 8.04672
    let max_nearby_mc_count = 5

    mcRequest(request.type_markers)
        .then(function(mcdata) {
            if (id !== current_id) return;

            if ('features' in mcdata === false) {
                format_and_send([], id);
                return;
            }

            mcdata.features.forEach(feature => {
                if (feature.geometry && Object.hasOwn(feature.geometry, 'coordinates')) {
                    feature.geometry.distance = mcCalculateDistance(feature.geometry.coordinates, coords);
                } else {
                    feature['geometry'] = { coordinates: [0,0], type: 'Point' };
                    feature.geometry.distance = mcCalculateDistance([0,0], coords);
                }
            });
        
            const results = mcdata.features.filter(feature => {
                return feature.geometry.distance <= radius;
            }).sort((a, b) => {
                return a.geometry.distance - b.geometry.distance;
            });
        
            const results_sliced = new Set(results.slice(0, max_nearby_mc_count));
    
            format_and_send(request.type_markers, results_sliced, id);
        });
}

function gps_success(pos) {
    if (id_gps !== current_id) return;
    var cur_location = [ pos.coords.latitude, pos.coords.longitude ];
    fetch_mcdata_and_sort_by_location(cur_location, id_gps);
}

function gps_error(err) {
    if (id_gps !== current_id) return;
    sendmcError(error.no_gps, id_gps);
}

function start_mc_gps(id) {
    var gps_options = {
        enableHighAccuracy: true,
        maximumAge: 30000,
        timeout: 12000
    };
    
    id_gps = id;

    mcRequest(request.type_markers)
        .then(function() {
            if (id !== current_id) return;
            navigator.geolocation.getCurrentPosition(gps_success, gps_error, gps_options);
        });
}

function fetch_mcdata_stats(id) {
    let mc_stat_count;

    try {
        var settings = JSON.parse(localStorage.getItem("clay-settings"));
    } catch (error) {
        console.log(error);
    }
    
    if (!settings || !settings.mc_stat_count) {
        mc_stat_count = 16;
    } else {
        mc_stat_count = settings.mc_stat_count;
    }

    mcRequest(request.type_stats)
        .then(function(mcdata) {
            const results = [];

            if (mcdata.broken) {
                results.push({
                    city: 'Currently Broken',
                    broken: mcdata.broken,
                    total_locations: 0
                });
            }
            
            if (mcdata.cities) {
                mcdata.cities.forEach(city => {
                    results.push(city);
                });
            }
            
            const results_sliced = new Set(results.slice(0, mc_stat_count));            

            format_and_send(request.type_stats, results_sliced, id);
        });
}

Pebble.addEventListener('ready', function() {
    Pebble.sendAppMessage({ 'mc_message': "mc_ready" });
    console.log('Im lovin it!');
});

Pebble.addEventListener('showConfiguration', function(e) {
    Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
    if (e && !e.response) {
        return;
    }
    
    var dict = clay.getSettings(e.response);
    Pebble.sendAppMessage({ 'mc_refresh': '' });
});

Pebble.addEventListener("appmessage", function(e) {
    current_id = e.payload.id;
    switch (e.payload.mc_message) {
        case 0:
            start_mc_gps(e.payload.id);
            break;
        case 1:
            fetch_mcdata_and_sort_by_saved(e.payload.id);
            break;
        case 2:
            fetch_mcdata_stats(e.payload.id);
            break;
    }
});

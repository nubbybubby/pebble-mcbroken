var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

/* This code is an NSFW warning (it sucks) */

var URL = 'https://mcbroken.com/markers.json'

var xhr;
var id_gps;
var current_id;
var is_loading;

let cache_max_age = 60 // seconds

let cache = [];
let then = [];

const err_connection_timed_out = "McConnection timed out."
const err_could_not_connect = "Could not connect to mcbroken."
const err_json_syntax = "McJSON Syntax Error."
const err_corrupt_json = "McJSON is mcbroken."
const err_no_gps = "Could not get location."
const err_no_loc_saved = "No locations saved!"
const err_no_loc_found = "No locations found!"

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

var send_id;

function mcSend(messages, id) {
    if (id !== undefined) {
        send_id = id;
    }
    
    if (messages.length === 0 || !is_loading || send_id !== current_id) return;
        message = messages.shift();
        Pebble.sendAppMessage(message, function() {
            mcSend(messages);
        },
        function (e) {
            console.log("I've McFallen! I'm Sorry! I've McFallen!");
        }
    );
}

function shut_up() {
    is_loading = false;
    current_id = 0;
}

function sendmcError(error_message, id, undefine_xhr) {
    shut_up();
    if (undefine_xhr) {
        xhr = undefined;
    }
    const message = { 
        'mc_message': "mc_error",
        'error': error_message,
        'id': id
    };
    Pebble.sendAppMessage(message);
}

function mcRequest(callback) {
    const now = new Date().getTime();

    // use ""cache"" if the data is less than a minute old
    if (Object.keys(cache).length > 0 && now - then < cache_max_age * 1000) {
        return callback(cache);
    } else if (Object.keys(cache).length > 0 && now - then >= cache_max_age * 1000) {
        xhr = undefined;
    }

    if (xhr) return;
    xhr = new XMLHttpRequest();
    xhr.timeout = 10000;
    xhr.responseType = 'json';
    xhr.onload = function() {
        if (xhr && xhr.status === 200 && xhr.readyState === 4) {
            try {
                then = new Date().getTime();
                cache = xhr.response;
                return callback(cache);
            } catch (error) {
                console.log(error);
                if (error instanceof SyntaxError) {
                    sendmcError(err_json_syntax, current_id, true);
                } else {
                    sendmcError(err_corrupt_json, current_id, true);
                }
                cache = [];
            }
        }
    };
    xhr.onloadend = function() {
        if (xhr && xhr.status == 404) {
            sendmcError(err_could_not_connect, current_id, true);
        }
    }
    xhr.onerror = function() {
        sendmcError(err_could_not_connect, current_id, true);
    }
    xhr.ontimeout = function() {
        sendmcError(err_connection_timed_out, current_id, true);
    }
 
    xhr.open('GET', URL, true);
    xhr.setRequestHeader('Content-Type', 'application/json');
    xhr.send();
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

function format_and_send(result, id) {
    let index = 0;

    const message = [];
    
    result.forEach(feature => {
        message.push(feature.properties);
        delete feature.properties.is_broken;
        delete feature.properties.is_active;
        delete feature.properties.state;
        delete feature.properties.country;

        feature.properties.mc_message = "mc_data";
        feature.properties.index = index;
        feature.properties.count = result.size;
        feature.properties.id = id;

        index++;
    });

    if (message.length > 0) {
        mcSend(message, id);
    } else {
        sendmcError(err_no_loc_found, id); 
    }
}

function fetch_mcdata_and_sort_by_saved(id) {
    is_loading = true;
    let max_saved_mc_count = 5
    
    var settings = JSON.parse(localStorage.getItem("clay-settings"));

    if (!settings) {
        sendmcError(err_no_loc_saved, id);
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
        sendmcError(err_no_loc_saved, id); 
        return;
    }

    const streets_input = streets_input_arr.map(element => element.toLowerCase().trim());
    
    const streets = streets_input.filter(Boolean);

    mcRequest(function(mcdata) {
        const results = [];
       
        /* This is kinda jank and inefficient. Whatever, it's fast enough */
        let has_pushed;
        for (let i = 0; i < streets.filter(Boolean).length; i++) {
            has_pushed = false;
            mcdata.features.filter(feature => {
                if (feature.properties.street.toString().toLowerCase().trim().includes(streets[i])) {
                    if (has_pushed || streets[i].length < 4) return;
                    results.push(feature);
                    has_pushed = true;
                }
            });
            if (!has_pushed) {
                results.push(structuredClone(not_found_feature));
            }
        }
        
        const results_sliced = new Set(results.slice(0, max_saved_mc_count));

        format_and_send(results_sliced, id);
    });
}

function fetch_mcdata_and_sort_by_location(coords, id) {
    let radius = 8.04672
    let max_nearby_mc_count = 5

    mcRequest(function(mcdata) {
        mcdata.features.forEach(feature => {
            feature.geometry.distance = mcCalculateDistance(feature.geometry.coordinates, coords);
        });

        const results = mcdata.features.filter(feature => {
            return feature.geometry.distance <= radius;
        }).sort((a, b) => {
            return a.geometry.distance - b.geometry.distance;
        });
        
        const results_sliced = new Set(results.slice(0, max_nearby_mc_count));

        format_and_send(results_sliced, id);
    }); 
}

function gps_success(pos) {
    if (!is_loading || id_gps !== current_id) return;
    var cur_location = [ pos.coords.latitude, pos.coords.longitude ];
    fetch_mcdata_and_sort_by_location(cur_location, id_gps);
}

function gps_error(err) {
    if (!is_loading || id_gps !== current_id) return;
    sendmcError(err_no_gps, id_gps);
}

function start_mc_gps(id) {
    is_loading = true;

    var gps_options = {
        enableHighAccuracy: false,
        maximumAge: 30000,
        timeout: 12000
    };
    
    id_gps = id;

    navigator.geolocation.getCurrentPosition(gps_success, gps_error, gps_options);
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
});

Pebble.addEventListener("appmessage", function(e) { 
    current_id = e.payload.id;
    switch (e.payload.mc_message) {
        case "load_mcdata_by_loc":
            start_mc_gps(e.payload.id);
            break;
        case "load_mcdata_by_saved":
            fetch_mcdata_and_sort_by_saved(e.payload.id);
            break;
        case "shut_up":
            shut_up();
            break;
    }
});

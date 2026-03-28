local mp = require 'mp'
local msg = require 'mp.msg'
local opts = require 'mp.options'

local o = {
    aid = "",
    cid = "",
    bvid = "",
    heartbeat_url = "http://127.0.0.1:8000/player/heartbeat",
    interval = 15,
}
opts.read_options(o, "bili")

local timer = nil

local function report_heartbeat()
    local time_pos = mp.get_property_number("time-pos", 0)
    if not time_pos then
        return
    end

    if o.aid == "" or o.cid == "" or o.bvid == "" then
        return
    end

    local played_time = math.floor(time_pos)
    local url = string.format(
        "%s?aid=%s&cid=%s&bvid=%s&played_time=%d",
        o.heartbeat_url,
        o.aid,
        o.cid,
        o.bvid,
        played_time
    )

    msg.info("report heartbeat: " .. url)
    mp.command_native_async({
        name = "subprocess",
        playback_only = false,
        capture_stdout = true,
        capture_stderr = true,
        args = { "curl", "-s", url }
    }, function(success, result, err)
        if not success then
            msg.error("heartbeat failed: " .. tostring(err))
        end
    end)
end

local function start_timer()
    if timer then
        timer:kill()
        timer = nil
    end
    timer = mp.add_periodic_timer(o.interval, report_heartbeat)
end

local function stop_timer()
    if timer then
        timer:kill()
        timer = nil
    end
end

mp.register_event("file-loaded", function()
    msg.info("bili heartbeat script loaded, aid=" .. o.aid .. ", cid=" .. o.cid .. ", bvid=" .. o.bvid)
    report_heartbeat()
    start_timer()
end)

mp.register_event("end-file", function()
    report_heartbeat()
    stop_timer()
end)

mp.observe_property("pause", "bool", function(_, paused)
    report_heartbeat()
    if paused then
        msg.info("paused, heartbeat reported")
    else
        msg.info("resumed, heartbeat reported")
    end
end)

mp.register_event("seek", function()
    report_heartbeat()
    msg.info("seek, heartbeat reported")
end)

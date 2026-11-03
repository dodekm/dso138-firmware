classdef DsoUsbClient < handle
    %DSOUSBCLIENT Read raw DSO138 captures over the DSO waveform protocol v1.
    % Requires MATLAB's serialport interface and a USB CDC COM port supplied
    % by the firmware backend. Baud rate is ignored by USB CDC. No DTR toggle.
    properties (SetAccess = private)
        Port
        Info
    end
    properties (Access = private)
        RxBuffer = uint8(zeros(0, 1))
        RequestId = uint16(0)
        JobId = uint32(0)
        ExportToken = uint32(0)
        Closed = false
    end

    methods
        function obj = DsoUsbClient(portName)
            arguments
                portName (1,1) string
            end
            obj.Port = serialport(portName, 115200, "Timeout", 0.2);
            obj.Info = obj.command(uint8(1), uint8([]));
            if obj.Info.Status ~= 0 || numel(obj.Info.Payload) ~= 8 || obj.Info.Payload(2) ~= 1
                clear obj
                error("DsoUsbClient:Protocol", "Device did not report waveform protocol version 1.");
            end
        end

        function frame = getHeldFrame(obj)
            frame = obj.downloadHeldFrame(uint32(0));
        end

        function frame = capture(obj, timeoutMs)
            arguments
                obj
                timeoutMs (1,1) uint32 = uint32(120000)
            end
            if timeoutMs < 1 || timeoutMs > 600000
                error("DsoUsbClient:Timeout", "Capture timeout must be from 1 to 600000 ms.");
            end
            reply = obj.command(uint8(6), DsoUsbClient.bytes32(timeoutMs));
            obj.requireOk(reply);
            if numel(reply.Payload) ~= 5
                error("DsoUsbClient:Protocol", "CAPTURE_SINGLE response length is invalid.");
            end
            obj.JobId = obj.readU32(reply.Payload, 2);
            cleanup = onCleanup(@() obj.cancelJob());
            start = tic;
            while toc(start) * 1000 < double(timeoutMs) + 1000
                statusReply = obj.command(uint8(2), DsoUsbClient.bytes32(obj.JobId));
                if statusReply.Status == 5
                    error("DsoUsbClient:CaptureTimeout", "The device capture timed out.");
                end
                obj.requireOk(statusReply);
                p = statusReply.Payload;
                if numel(p) ~= 13 || obj.readU32(p, 6) ~= obj.JobId
                    error("DsoUsbClient:Protocol", "STATUS response does not match the capture job.");
                end
                state = p(5); % HostCaptureState: None, Pending, Complete, Cancelled, Fault
                if state == 2
                    frame = obj.downloadHeldFrame(obj.JobId);
                    obj.JobId = uint32(0);
                    clear cleanup
                    return
                elseif state == 3
                    error("DsoUsbClient:Cancelled", "The host capture was cancelled by local instrument activity.");
                elseif state == 4
                    error("DsoUsbClient:CaptureFault", "The host capture failed.");
                elseif state == 0
                    error("DsoUsbClient:StaleCapture", "The device no longer recognizes this capture job.");
                end
                pause(0.02);
            end
            error("DsoUsbClient:CaptureTimeout", "Timed out while waiting for the device capture.");
        end

        function frames = captureRepeated(obj, count, timeoutMs, gapSeconds)
            % Each capture is separate; gapSeconds is an explicit dead-time delay.
            arguments
                obj
                count (1,1) double {mustBeInteger,mustBePositive}
                timeoutMs (1,1) uint32 = uint32(120000)
                gapSeconds (1,1) double {mustBeNonnegative} = 0
            end
            frames = cell(count, 1);
            for k = 1:count
                frames{k} = obj.capture(timeoutMs);
                if k < count && gapSeconds > 0
                    pause(gapSeconds);
                end
            end
        end

        function delete(obj)
            if ~obj.Closed
                obj.cancelJob();
                obj.releaseExport();
                obj.Closed = true;
                if ~isempty(obj.Port)
                    delete(obj.Port);
                end
            end
        end
    end

    methods (Access = private)
        function frame = downloadHeldFrame(obj, expectedJob)
            if expectedJob == 0
                beginReply = obj.command(uint8(3), uint8([]));
                if beginReply.Status == 2
                    error("DsoUsbClient:FrameBusy", "No held frame is ready for export; the device returned Busy.");
                end
                obj.requireOk(beginReply);
                preBeginStatus = [];
            else
                [beginReply, preBeginStatus] = obj.beginCaptureFrame(expectedJob);
            end
            if numel(beginReply.Payload) ~= 48
                error("DsoUsbClient:Frame", "BEGIN_FRAME metadata length is invalid.");
            end
            meta = obj.decodeMetadata(beginReply.Payload);
            obj.ExportToken = meta.Token;
            cleanup = onCleanup(@() obj.releaseExport());
            if expectedJob ~= 0
                p = obj.captureStatusPayload(obj.command(uint8(2), DsoUsbClient.bytes32(expectedJob)), expectedJob);
                if ~obj.statusReadyForExport(p) || p(5) ~= 2 || obj.readU16(p, 11) ~= meta.Count || ...
                        obj.readU32(p, 6) ~= obj.readU32(preBeginStatus, 6)
                    error("DsoUsbClient:Cancelled", "The completed capture no longer matches the held frame.");
                end
            end
            codes = zeros(double(meta.Count), 1, "uint16");
            offset = uint16(0);
            while offset < meta.Count
                n = uint8(min(22, double(meta.Count - offset)));
                request = [DsoUsbClient.bytes32(meta.Token); DsoUsbClient.bytes16(offset); n];
                reply = obj.command(uint8(4), request);
                obj.requireOk(reply);
                p = reply.Payload;
                if numel(p) ~= 8 + 2*double(n) || obj.readU32(p, 2) ~= meta.Token || ...
                        obj.readU16(p, 6) ~= offset || p(8) ~= n
                    error("DsoUsbClient:Frame", "READ_SAMPLES response does not match the requested range.");
                end
                for k = 1:double(n)
                    codes(double(offset) + k) = obj.readU16(p, 9 + 2*(k-1));
                end
                offset = offset + uint16(n);
            end
            if any(codes > 4095)
                error("DsoUsbClient:Frame", "Device returned a sample outside the 12-bit ADC range.");
            end
            reply = obj.command(uint8(5), DsoUsbClient.bytes32(meta.Token));
            obj.requireOk(reply);
            obj.ExportToken = uint32(0);
            clear cleanup
            frame = meta;
            frame.Raw = codes;
            frame.TimeSeconds = ((0:double(meta.Count)-1).' - double(meta.Pretrigger)) .* ...
                (double(meta.Divider) / double(meta.TimerHz));
            frame.VoltageMicrovolts = obj.scaleCodes(codes, meta);
            frame.VoltageVolts = double(frame.VoltageMicrovolts) .* 1e-6;
        end
        function [beginReply, readyStatus] = beginCaptureFrame(obj, expectedJob)
            % The device can report HostCaptureState::Complete before its
            % bounded HOLD redraw finishes. Wait for that foreground work,
            % then use a new request ID for every Busy BEGIN attempt.
            maxWaitSeconds = 1.0;
            started = tic;
            while toc(started) < maxWaitSeconds
                remaining = maxWaitSeconds - toc(started);
                if remaining <= 0
                    break
                end
                statusReply = obj.command(uint8(2), DsoUsbClient.bytes32(expectedJob), min(remaining, 0.25));
                p = obj.captureStatusPayload(statusReply, expectedJob);
                readyStatus = p;
                state = p(5);
                if state == 3
                    error("DsoUsbClient:Cancelled", "Local instrument activity superseded this capture before frame export.");
                elseif state == 4
                    error("DsoUsbClient:CaptureFault", "The host capture failed before frame export.");
                end
                if state == 2 && obj.statusReadyForExport(p)
                    remaining = maxWaitSeconds - toc(started);
                    if remaining <= 0
                        break
                    end
                    beginReply = obj.command(uint8(3), uint8([]), min(remaining, 0.25));
                    if beginReply.Status == 0
                        return
                    elseif beginReply.Status ~= 2
                        obj.requireOk(beginReply);
                    end
                end
                pause(0.01);
            end
            error("DsoUsbClient:FrameBusy", ...
                "The completed capture did not become exportable within 1 second; no other frame was substituted.");
        end
        function p = captureStatusPayload(obj, statusReply, expectedJob)
            if statusReply.Status == 5
                error("DsoUsbClient:CaptureTimeout", "The device capture timed out before frame export.");
            elseif statusReply.Status == 3
                error("DsoUsbClient:StaleCapture", "The device no longer recognizes this capture job.");
            end
            obj.requireOk(statusReply);
            p = statusReply.Payload;
            if numel(p) ~= 13 || obj.readU32(p, 6) ~= expectedJob
                error("DsoUsbClient:Protocol", "STATUS response does not match the capture job.");
            end
            if p(5) == 3
                error("DsoUsbClient:Cancelled", "Local instrument activity superseded this capture before frame export.");
            elseif p(5) == 4
                error("DsoUsbClient:CaptureFault", "The host capture failed before frame export.");
            elseif p(5) == 0
                error("DsoUsbClient:StaleCapture", "The device no longer recognizes this capture job.");
            end
        end
        function ready = statusReadyForExport(~, p)
            ready = numel(p) == 13 && p(2) == 0 && p(4) == 0 && p(10) == 1 && p(13) == 0;
        end
        function reply = command(obj, opcode, payload, maxWaitSeconds)
            if nargin < 4
                maxWaitSeconds = 3.0;
            end
            if obj.Closed
                error("DsoUsbClient:Closed", "The serial connection is closed.");
            end
            obj.RequestId = uint16(mod(double(obj.RequestId), 65535) + 1);
            frame = obj.encodeRequest(opcode, obj.RequestId, payload);
            deadline = tic;
            retries = 0;
            write(obj.Port, frame, "uint8");
            while toc(deadline) < maxWaitSeconds
                if obj.Port.NumBytesAvailable > 0
                    n = min(obj.Port.NumBytesAvailable, 64);
                    received = uint8(read(obj.Port, n, "uint8"));
                    obj.RxBuffer = [obj.RxBuffer(:); received(:)];
                end
                [found, response, consumed] = obj.takeResponse(obj.RequestId, bitor(opcode, uint8(128)));
                if consumed > 0
                    obj.RxBuffer(1:consumed) = [];
                end
                if found
                    reply = response;
                    return
                end
                if toc(deadline) > min(0.75, maxWaitSeconds / 2) && retries < 2
                    % Reuse request ID and exact bytes: firmware replay is idempotent.
                    write(obj.Port, frame, "uint8");
                    retries = retries + 1;
                end
                pause(0.005);
            end
            error("DsoUsbClient:Timeout", "No valid response arrived for opcode %u within %.2f s.", opcode, maxWaitSeconds);
        end

        function [found, response, consumed] = takeResponse(obj, requestId, expectedOpcode)
            found = false; response = struct(); consumed = 0;
            b = uint8(obj.RxBuffer(:));
            while numel(b) - consumed >= 2 && ...
                    ~(b(consumed+1) == hex2dec("44") && b(consumed+2) == hex2dec("53"))
                consumed = consumed + 1;
            end
            if numel(b) - consumed < 12, return; end
            h = b(consumed+1:end);
            length = double(obj.readU16(h, 7));
            total = 12 + length;
            if h(3) ~= 1 || length > 52 || total > 64
                consumed = consumed + 1;
                return
            end
            if numel(h) < total, return; end
            payload = h(13:total);
            actual = obj.crc32([h(1:8); payload]);
            if actual ~= obj.readU32(h, 9)
                consumed = consumed + 1;
                return
            end
            responseId = obj.readU16(h, 5);
            responseOpcode = h(4);
            if responseId ~= requestId || responseOpcode ~= expectedOpcode
                % A timed-out identical request may yield a replay after its first reply.
                consumed = consumed + total;
                return
            end
            if isempty(payload)
                error("DsoUsbClient:Protocol", "Response omitted its status byte.");
            end
            response = struct("Status", payload(1), "Payload", payload);
            consumed = total;
            found = true;
        end

        function requireOk(~, reply)
            if reply.Status ~= 0
                names = ["OK","Invalid","Busy","Stale","Unsupported","Timeout","Cancelled","Fault"];
                code = double(reply.Status) + 1;
                if code <= numel(names), name = names(code); else, name = "Unknown"; end
                error("DsoUsbClient:Device", "Device returned %s (%u).", name, reply.Status);
            end
        end

        function meta = decodeMetadata(obj, p)
            if p(1) ~= 0, error("DsoUsbClient:Device", "BEGIN_FRAME failed."); end
            meta = struct();
            meta.Token = obj.readU32(p, 2);
            meta.Count = obj.readU16(p, 6);
            meta.Pretrigger = obj.readU16(p, 8);
            meta.TimerHz = obj.readU32(p, 10);
            meta.Divider = obj.readU32(p, 14);
            meta.AcquisitionEpoch = obj.readU32(p, 18);
            meta.TriggerSequence = obj.readU32(p, 22);
            meta.PhysicalGeneration = obj.readU32(p, 26);
            meta.MicrovoltsPerDivision = obj.readU32(p, 30);
            meta.ZeroCodeQ16 = typecast(obj.readU32(p, 34), "int32");
            meta.GainQ16 = obj.readU32(p, 38);
            meta.GainShift = p(42);
            meta.CalibrationKind = p(43);
            meta.Coupling = p(44);
            meta.TriggerMode = p(45);
            meta.TriggerEdge = p(46);
            meta.Flags = p(47);
            if p(48) ~= 0 || meta.Count < 1 || meta.Count > 1024 || ...
                    meta.Pretrigger >= meta.Count || meta.TimerHz == 0 || meta.Divider == 0 || meta.Token == 0
                error("DsoUsbClient:Frame", "Frame metadata is outside protocol bounds.");
            end
            if meta.CalibrationKind > 2 || meta.Coupling > 3 || meta.TriggerMode > 2 || ...
                    meta.TriggerEdge > 1 || bitand(meta.Flags, uint8(240)) ~= 0
                error("DsoUsbClient:Frame", "Frame metadata contains an unknown enum or flag.");
            end
        end

        function values = scaleCodes(~, codes, meta)
            % Return [] for unavailable calibration; raw ADC codes remain authoritative.
            if meta.CalibrationKind == 0
                values = [];
                return
            end
            if meta.ZeroCodeQ16 < 0 || meta.ZeroCodeQ16 > int32(4095 * 65536) || ...
                    meta.GainQ16 == 0 || meta.GainShift > 2 || ...
                    (meta.GainShift ~= 0 && meta.GainQ16 < uint32(hex2dec("80000000")))
                error("DsoUsbClient:Calibration", "Calibration metadata is outside firmware bounds.");
            end
            values = zeros(size(codes), "int64");
            denominatorBits = uint8(32 - meta.GainShift);
            half = bitshift(uint64(1), denominatorBits - 1);
            for k = 1:numel(codes)
                deltaQ16 = int64(codes(k)) * int64(65536) - int64(meta.ZeroCodeQ16);
                magnitude = uint64(abs(deltaQ16)) * uint64(meta.GainQ16);
                rounded = bitshift(magnitude + half, -double(denominatorBits));
                if deltaQ16 < 0
                    values(k) = -int64(rounded);
                else
                    values(k) = int64(rounded);
                end
            end
        end

        function releaseExport(obj)
            if obj.ExportToken ~= 0 && ~obj.Closed
                token = obj.ExportToken;
                obj.ExportToken = uint32(0);
                try
                    obj.command(uint8(5), DsoUsbClient.bytes32(token));
                catch
                end
            end
        end

        function cancelJob(obj)
            if obj.JobId ~= 0 && ~obj.Closed
                job = obj.JobId;
                obj.JobId = uint32(0);
                try
                    obj.command(uint8(7), DsoUsbClient.bytes32(job));
                catch
                end
            end
        end
    end

    methods (Static, Access = private)
        function frame = encodeRequest(opcode, requestId, payload)
            payload = uint8(payload(:));
            if numel(payload) > 52
                error("DsoUsbClient:Request", "Request payload exceeds 52 bytes.");
            end
            h = zeros(12 + numel(payload), 1, "uint8");
            h(1:4) = uint8([hex2dec("44"); hex2dec("53"); 1; opcode]);
            h(5:6) = DsoUsbClient.bytes16(requestId);
            h(7:8) = DsoUsbClient.bytes16(uint16(numel(payload)));
            h(13:end) = payload;
            crcInput = [h(1:8); payload];
            h(9:12) = DsoUsbClient.bytes32(DsoUsbClient.crc32(crcInput));
            frame = h;
        end
        function bytes = bytes16(v)
            bytes = uint8([bitand(v, uint16(255)); bitshift(v, -8)]);
        end
        function bytes = bytes32(v)
            bytes = uint8([bitand(v,uint32(255)); bitand(bitshift(v,-8),uint32(255)); ...
                bitand(bitshift(v,-16),uint32(255)); bitshift(v,-24)]);
        end
        function v = readU16(b, offset)
            v = uint16(b(offset)) + bitshift(uint16(b(offset+1)), 8);
        end
        function v = readU32(b, offset)
            v = uint32(b(offset)) + bitshift(uint32(b(offset+1)), 8) + ...
                bitshift(uint32(b(offset+2)), 16) + bitshift(uint32(b(offset+3)), 24);
        end
        function crc = crc32(bytes)
            crc = uint32(hex2dec("FFFFFFFF"));
            polynomial = uint32(hex2dec("EDB88320"));
            for k = 1:numel(bytes)
                crc = bitxor(crc, uint32(bytes(k)));
                for bit = 1:8
                    if bitand(crc, uint32(1)) ~= 0
                        crc = bitxor(bitshift(crc, -1), polynomial);
                    else
                        crc = bitshift(crc, -1);
                    end
                end
            end
            crc = bitcmp(crc);
        end
    end
end

using System;
using System.Globalization;
using System.Text.RegularExpressions;

namespace ASCOM.InEquator
{
    internal sealed class TrackerDeviceInfo
    {
        public int? PositionSteps { get; set; }
        public bool? Tracking { get; set; }
        public int? JogRate { get; set; }
        public bool? IsMoving { get; set; }
        public bool? Hold { get; set; }
        public bool? Reversed { get; set; }
        public int? StepsPerOutputRev { get; set; }
        public double? TrackingRate { get; set; }
        public long? Ppm { get; set; }
    }

    internal static class TrackerProtocol
    {
        private static readonly Regex MotionStatusPattern = new Regex(
            @"^\s*P\s+(-?\d+)\s*;\s*T\s+(true|false)\s*;\s*Q\s+(-?\d+)\s*;\s*Y\s+(-?\d+)\s*;\s*M\s+(true|false)\s*#?\s*$",
            RegexOptions.Compiled | RegexOptions.CultureInvariant | RegexOptions.IgnoreCase);

        internal static bool TryParseMotionStatus(string response, out int position, out bool tracking,
            out int jogRate, out int jogStepsPerSec, out bool moving)
        {
            position = 0;
            tracking = false;
            jogRate = 0;
            jogStepsPerSec = 0;
            moving = false;

            Match match = MotionStatusPattern.Match(response ?? string.Empty);
            if (!match.Success) return false;
            if (!int.TryParse(match.Groups[1].Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out position)) return false;
            if (!bool.TryParse(match.Groups[2].Value, out tracking)) return false;
            if (!int.TryParse(match.Groups[3].Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out jogRate)) return false;
            if (!int.TryParse(match.Groups[4].Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out jogStepsPerSec)) return false;
            if (!bool.TryParse(match.Groups[5].Value, out moving)) return false;
            return true;
        }

        internal static TrackerDeviceInfo ParseDeviceInfo(string response)
        {
            return new TrackerDeviceInfo
            {
                PositionSteps = ParseNullableInt(response, "positionSteps"),
                Tracking = ParseNullableBool(response, "tracking"),
                JogRate = ParseNullableInt(response, "jogRate"),
                IsMoving = ParseNullableBool(response, "isMoving"),
                Hold = ParseNullableBool(response, "hold"),
                Reversed = ParseNullableBool(response, "reversed"),
                StepsPerOutputRev = ParseNullableInt(response, "stepsPerOutputRev"),
                TrackingRate = ParseNullableDouble(response, "trackingRate"),
                Ppm = ParseNullableLong(response, "ppm")
            };
        }

        internal static bool IsErrorResponse(string response)
        {
            return !string.IsNullOrWhiteSpace(response)
                && response.TrimStart().StartsWith("ERR:", StringComparison.OrdinalIgnoreCase);
        }

        private static int? ParseNullableInt(string response, string propertyName)
        {
            Match match = MatchJsonValue(response, propertyName, @"-?\d+");
            int value;
            if (match.Success && int.TryParse(match.Groups[1].Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out value))
                return value;
            return null;
        }

        private static long? ParseNullableLong(string response, string propertyName)
        {
            Match match = MatchJsonValue(response, propertyName, @"-?\d+");
            long value;
            if (match.Success && long.TryParse(match.Groups[1].Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out value))
                return value;
            return null;
        }

        private static double? ParseNullableDouble(string response, string propertyName)
        {
            Match match = MatchJsonValue(response, propertyName, @"-?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?");
            double value;
            if (match.Success && double.TryParse(match.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out value))
                return value;
            return null;
        }

        private static bool? ParseNullableBool(string response, string propertyName)
        {
            Match match = MatchJsonValue(response, propertyName, @"true|false");
            bool value;
            if (match.Success && bool.TryParse(match.Groups[1].Value, out value))
                return value;
            return null;
        }

        private static Match MatchJsonValue(string response, string propertyName, string valuePattern)
        {
            return Regex.Match(
                response ?? string.Empty,
                "\\\"" + Regex.Escape(propertyName) + "\\\"\\s*:\\s*(" + valuePattern + ")",
                RegexOptions.CultureInvariant | RegexOptions.IgnoreCase);
        }
    }
}

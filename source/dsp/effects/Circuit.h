#pragma once

#include <array>
#include <cmath>
#include <utility>

namespace effects
{
    // An op-amp output pushed towards its supply: straight through, then
    // levelling off smoothly at `rail` volts either way. Firm but rounded:
    // an op-amp stays clean almost to its rail, then flattens.
    inline double opAmpClip (double volts, double rail) noexcept
    {
        // volts / (1 + |volts / rail|^5)^(1/5). (PR7-K: the fifth power by multiplying, and below 6% of the rail
        // the series 1 - x/5 + 3x^2/25, exact to 1e-18 there: the same curve for a third of the cost; it ran four
        // times a sample in AEON, two pows each.)
        const auto r = std::abs (volts) / rail, r2 = r * r, r5 = r2 * r2 * r;
        if (r5 < 1.0e-6)
            return volts * (1.0 - 0.2 * r5 + 0.12 * r5 * r5);
        return volts / std::pow (1.0 + r5, 0.2);
    }

    // A diode pair's sinh (x) and cosh (x), from one exp (PR7-K: the diode solvers called both, two library calls
    // that each work out the exponential again; this was most of the drives' cost). The same values to rounding.
    struct SinhCosh { double sinh, cosh; };
    inline SinhCosh sinhCosh (double x) noexcept
    {
        const auto e = std::exp (x), inverse = 1.0 / e;
        return { 0.5 * (e - inverse), 0.5 * (e + inverse) };
    }

    // A small linear circuit (resistors, capacitors and ideal op-amps, one
    // input and one output) turned into a digital filter that behaves like
    // it. Used to model pedal stages from their schematics: each stage is
    // written down as its parts list, with the real component values, and
    // the response follows from those, rather than from filter shapes we
    // picked by ear.
    //
    // How: nodal analysis, the method circuit simulators use, with each
    // capacitor replaced by its trapezoidal-rule equivalent (a resistor and
    // a current source carrying its history). That's the same as the
    // bilinear transform, so it's stable at any setting and its frequency
    // response matches the circuit's, slightly squeezed close to half the
    // sample rate. Solving the network once per change of a part's value
    // (a pot turned, a switch flipped) leaves a small state-space filter to
    // run per sample: one state per capacitor.
    //
    // Node 0 is ground: in the pedal, that's both 0 V and the 4.5 V bias the
    // op-amps sit on, which are the same for the audio signal. Everything is
    // fixed-size, so update() can run on the audio thread without allocating.
    class Circuit
    {
    public:
        static constexpr int maxNodes = 16, maxResistors = 24, maxCapacitors = 12, maxOpAmps = 4;

        int node() noexcept { return numNodes++; }   // returns the new node's number
        int resistor (int a, int b, double ohms) noexcept { resistors[(size_t) numResistors] = { a, b, ohms }; return numResistors++; }
        int capacitor (int a, int b, double farads) noexcept { capacitors[(size_t) numCapacitors] = { a, b, farads }; return numCapacitors++; }
        void opAmp (int plus, int minus, int out) noexcept { opAmps[(size_t) numOpAmps++] = { plus, minus, out }; }
        void setInput (int n) noexcept { inputNode = n; }
        void setOutput (int n) noexcept { outputNode = n; }

        // Change a part's value (e.g. half of a pot). Call update() afterwards.
        void setResistor (int index, double ohms) noexcept { resistors[(size_t) index].value = std::fmax (ohms, 1.0e-3); }
        void setCapacitor (int index, double farads) noexcept { capacitors[(size_t) index].value = farads; }

        void prepare (double sampleRate) noexcept
        {
            period = 1.0 / sampleRate;
            update();
            reset();
        }

        void reset() noexcept { state.fill (0.0); }

        // Re-solves the network for the current part values, keeping the
        // capacitors' charge, so a pot can move while audio plays.
        void update() noexcept
        {
            const auto unknowns = (numNodes - 1) + 1 + numOpAmps;   // node voltages, the input's current, each op-amp's output current
            const auto columns = numCapacitors + 1;                  // one right-hand side per capacitor history, one for the input
            Matrix m {};

            const auto stamp = [&m] (int a, int b, double g)
            {
                if (a > 0) m[(size_t) a - 1][(size_t) a - 1] += g;
                if (b > 0) m[(size_t) b - 1][(size_t) b - 1] += g;
                if (a > 0 && b > 0)
                {
                    m[(size_t) a - 1][(size_t) b - 1] -= g;
                    m[(size_t) b - 1][(size_t) a - 1] -= g;
                }
            };
            for (int i = 0; i < numResistors; ++i)
                stamp (resistors[(size_t) i].a, resistors[(size_t) i].b, 1.0 / resistors[(size_t) i].value);
            for (int k = 0; k < numCapacitors; ++k)
            {
                conductance[(size_t) k] = 2.0 * capacitors[(size_t) k].value / period;
                stamp (capacitors[(size_t) k].a, capacitors[(size_t) k].b, conductance[(size_t) k]);
            }

            // The input: an ideal voltage source from ground.
            const auto sourceRow = (size_t) numNodes - 1;
            m[(size_t) inputNode - 1][sourceRow] += 1.0;
            m[sourceRow][(size_t) inputNode - 1] += 1.0;
            // Each ideal op-amp: its output supplies whatever current keeps its inputs equal.
            for (int i = 0; i < numOpAmps; ++i)
            {
                const auto row = sourceRow + 1 + (size_t) i;
                const auto& amp = opAmps[(size_t) i];
                m[(size_t) amp.out - 1][row] += 1.0;
                if (amp.plus > 0) m[row][(size_t) amp.plus - 1] += 1.0;
                if (amp.minus > 0) m[row][(size_t) amp.minus - 1] -= 1.0;
            }

            // Right-hand sides: each capacitor's history pushes current into
            // one of its nodes and out of the other; the input sets its voltage.
            Matrix rhs {};
            for (int k = 0; k < numCapacitors; ++k)
            {
                if (capacitors[(size_t) k].a > 0) rhs[(size_t) capacitors[(size_t) k].a - 1][(size_t) k] += 1.0;
                if (capacitors[(size_t) k].b > 0) rhs[(size_t) capacitors[(size_t) k].b - 1][(size_t) k] -= 1.0;
            }
            rhs[sourceRow][(size_t) numCapacitors] = 1.0;

            solve (m, rhs, unknowns, columns);   // rhs now holds the node voltages per unit of each source

            const auto voltage = [&rhs] (int n, int column) { return n > 0 ? rhs[(size_t) n - 1][(size_t) column] : 0.0; };
            for (int k = 0; k < numCapacitors; ++k)
            {
                const auto& c = capacitors[(size_t) k];
                for (int j = 0; j <= numCapacitors; ++j)
                {
                    // The capacitor's voltage per unit of each source, and what that does to its history.
                    const auto v = voltage (c.a, j) - voltage (c.b, j);
                    const auto next = 2.0 * conductance[(size_t) k] * v - (j == k ? 1.0 : 0.0);
                    if (j < numCapacitors)
                        stateMatrix[(size_t) k][(size_t) j] = next;
                    else
                        inputToState[(size_t) k] = next;
                }
            }
            for (int j = 0; j < numCapacitors; ++j)
                stateToOutput[(size_t) j] = voltage (outputNode, j);
            inputToOutput = voltage (outputNode, numCapacitors);
        }

        // The output is feedthrough() * input + pending(): what the next
        // process() will give, split so a nonlinear part around the circuit
        // can solve for its input first (e.g. an op-amp driving it).
        double feedthrough() const noexcept { return inputToOutput; }
        double pending() const noexcept
        {
            auto out = 0.0;
            for (int k = 0; k < numCapacitors; ++k)
                out += stateToOutput[(size_t) k] * state[(size_t) k];
            return out;
        }

        double process (double in) noexcept
        {
            auto out = inputToOutput * in;
            std::array<double, maxCapacitors> next;   // (PR7-K: only the capacitors in use set and copied)
            for (int k = 0; k < numCapacitors; ++k)
            {
                out += stateToOutput[(size_t) k] * state[(size_t) k];
                auto sum = inputToState[(size_t) k] * in;
                for (int j = 0; j < numCapacitors; ++j)
                    sum += stateMatrix[(size_t) k][(size_t) j] * state[(size_t) j];
                next[(size_t) k] = sum;
            }
            std::copy (next.begin(), next.begin() + numCapacitors, state.begin());
            return out;
        }

    private:
        static constexpr int maxUnknowns = maxNodes + maxOpAmps;
        using Matrix = std::array<std::array<double, maxUnknowns>, maxUnknowns>;

        // Gaussian elimination with partial pivoting: solves m * x = rhs for
        // the first `columns` columns of rhs, in place.
        static void solve (Matrix& m, Matrix& rhs, int size, int columns) noexcept
        {
            for (int col = 0; col < size; ++col)
            {
                auto pivot = col;
                for (int row = col + 1; row < size; ++row)
                    if (std::abs (m[(size_t) row][(size_t) col]) > std::abs (m[(size_t) pivot][(size_t) col]))
                        pivot = row;
                std::swap (m[(size_t) col], m[(size_t) pivot]);
                std::swap (rhs[(size_t) col], rhs[(size_t) pivot]);

                const auto diagonal = m[(size_t) col][(size_t) col];
                if (std::abs (diagonal) < 1.0e-30)
                    continue;   // a floating node: leave it at 0 V rather than produce NaNs
                for (int row = 0; row < size; ++row)
                {
                    if (row == col)
                        continue;
                    const auto factor = m[(size_t) row][(size_t) col] / diagonal;
                    for (int j = col; j < size; ++j)
                        m[(size_t) row][(size_t) j] -= factor * m[(size_t) col][(size_t) j];
                    for (int j = 0; j < columns; ++j)
                        rhs[(size_t) row][(size_t) j] -= factor * rhs[(size_t) col][(size_t) j];
                }
            }
            for (int row = 0; row < size; ++row)
            {
                const auto diagonal = m[(size_t) row][(size_t) row];
                for (int j = 0; j < columns; ++j)
                    rhs[(size_t) row][(size_t) j] = std::abs (diagonal) < 1.0e-30 ? 0.0 : rhs[(size_t) row][(size_t) j] / diagonal;
            }
        }

        struct Part { int a = 0, b = 0; double value = 1.0; };
        struct OpAmp { int plus = 0, minus = 0, out = 0; };

        int numNodes = 1, numResistors = 0, numCapacitors = 0, numOpAmps = 0;   // node 0 is ground
        int inputNode = 1, outputNode = 1;
        std::array<Part, maxResistors> resistors {};
        std::array<Part, maxCapacitors> capacitors {};
        std::array<OpAmp, maxOpAmps> opAmps {};
        double period = 1.0 / 48000.0;

        std::array<double, maxCapacitors> conductance {}, state {}, inputToState {}, stateToOutput {};
        std::array<std::array<double, maxCapacitors>, maxCapacitors> stateMatrix {};
        double inputToOutput = 0.0;
    };
}

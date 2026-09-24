// EXPECT-ERROR: no provider registered for 'st'
import st.Transcript as T
@main def run(): Unit = println(T)

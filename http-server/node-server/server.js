import express from "express";
import fs from "fs";

const app = express();

app.get("/", (req, res) => {
    res.send("Hallo van server")
})

app.post("/", (req, res) => {
    let buffer = []
    req.on("data", (data) => {
        buffer.push(Buffer.from(data))
    })

    req.on("end", () => {
        buffer = Buffer.concat(buffer)
        fs.writeFileSync("foto.jpeg", buffer)
        res.end("Foto succesvol geupload!")
    })
})


const PORT = 3000;
app.listen(PORT, () => {
    console.log(`Server running on port: ${PORT}`)
})
